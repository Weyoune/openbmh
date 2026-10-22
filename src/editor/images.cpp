// SPDX-License-Identifier: GPL-3.0-only
#include "editor/images.hpp"
#include <SDL.h>
#include <SDL_image.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <lunasvg.h>
#include <miniz.h>
#include <pugixml.hpp>
#include <sstream>

namespace bmh::editor::images {
namespace {
constexpr uint64_t maximum_pixels = 32ull * 1024 * 1024;
void bounds(int w, int h) {
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || uint64_t(w) * uint64_t(h) > maximum_pixels)
        throw Error("image_dimensions");
}
struct Geometry {
    double angle = 0, x = 0, y = 0, width = 1, height = 1;
    bool flip_x = false, flip_y = false;
    explicit Geometry(const Json &input) {
        const Json value = input.is_null() ? Json::object() : input;
        if (!value.is_object())
            throw Error("invalid_transform");
        angle = value.value("angle", 0.0);
        flip_x = value.value("flip_x", false);
        flip_y = value.value("flip_y", false);
        if (value.contains("crop")) {
            const auto &crop = value["crop"];
            x = crop.value("x", 0.0);
            y = crop.value("y", 0.0);
            width = crop.value("width", 1.0);
            height = crop.value("height", 1.0);
        }
        if (!std::isfinite(angle) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
            !std::isfinite(height) || std::abs(angle) > 360 || x < 0 || y < 0 || width <= 0 || height <= 0 ||
            x + width > 1.000001 || y + height > 1.000001)
            throw Error("invalid_transform");
    }
    Json json() const {
        return {{"angle", angle},
                {"flip_x", flip_x},
                {"flip_y", flip_y},
                {"crop", {{"x", x}, {"y", y}, {"width", width}, {"height", height}}}};
    }
};
std::pair<double, double> canvas(double w, double h, double angle) {
    const auto rad = angle * std::acos(-1.0) / 180.0;
    auto c = std::abs(std::cos(rad)), s = std::abs(std::sin(rad));
    return {std::ceil(w * c + h * s - 1e-9), std::ceil(w * s + h * c - 1e-9)};
}
void validate_svg(pugi::xml_node node) {
    for (auto attr : node.attributes()) {
        const std::string key = attr.name(), value = attr.value();
        const auto lower = folded(value);
        if ((key == "href" || key == "xlink:href") && std::string(node.name()) != "a" && !value.empty() &&
            value.front() != '#' && !value.starts_with("data:image/"))
            throw Error("external_svg_resource");
        if (lower.find("url(") != lower.npos && lower.find("url(#") == lower.npos &&
            lower.find("url('#") == lower.npos && lower.find("url(\"#") == lower.npos)
            throw Error("external_svg_resource");
    }
    if (std::string(node.name()) == "style") {
        const std::string css = folded(node.text().as_string());
        if (css.find("@import") != css.npos || css.find("http:") != css.npos || css.find("https:") != css.npos ||
            css.find("file:") != css.npos)
            throw Error("external_svg_resource");
    }
    for (auto child : node.children())
        validate_svg(child);
}
std::unique_ptr<lunasvg::Document> svg_document(std::string_view bytes) {
    pugi::xml_document xml;
    if (!xml.load_buffer(bytes.data(), bytes.size()))
        throw Error("invalid_image");
    validate_svg(xml.document_element());
    auto doc = lunasvg::Document::loadFromData(bytes.data(), bytes.size());
    if (!doc || doc->width() <= 0 || doc->height() <= 0 || !std::isfinite(doc->width()) ||
        !std::isfinite(doc->height()))
        throw Error("invalid_image");
    return doc;
}
std::string transform_svg(std::string_view bytes, const Geometry &g, int &width, int &height) {
    auto source = svg_document(bytes);
    const double w = source->width(), h = source->height();
    const auto [cw, ch] = canvas(w, h, g.angle);
    const auto ow = std::max(1.0, cw * g.width), oh = std::max(1.0, ch * g.height);
    if (ow > 1000000 || oh > 1000000)
        throw Error("image_dimensions");
    width = int(std::round(ow));
    height = int(std::round(oh));
    pugi::xml_document original, output;
    if (!original.load_buffer(bytes.data(), bytes.size()))
        throw Error("invalid_image");
    auto root = output.append_child("svg");
    root.append_attribute("xmlns") = "http://www.w3.org/2000/svg";
    root.append_attribute("xmlns:xlink") = "http://www.w3.org/1999/xlink";
    root.append_attribute("width") = ow;
    root.append_attribute("height") = oh;
    const auto view = "0 0 " + std::to_string(ow) + ' ' + std::to_string(oh);
    root.append_attribute("viewBox") = view.c_str();
    auto group = root.append_child("g");
    std::ostringstream matrix;
    matrix.imbue(std::locale::classic());
    matrix << "translate(" << -g.x * cw << ' ' << -g.y * ch << ") translate(" << cw / 2 << ' ' << ch / 2 << ") rotate("
           << g.angle << ") scale(" << (g.flip_x ? -1 : 1) << ' ' << (g.flip_y ? -1 : 1) << ") translate(" << -w / 2
           << ' ' << -h / 2 << ')';
    group.append_attribute("transform") = matrix.str().c_str();
    auto child = group.append_copy(original.document_element());
    if (!child.attribute("width"))
        child.append_attribute("width");
    if (!child.attribute("height"))
        child.append_attribute("height");
    child.attribute("width") = w;
    child.attribute("height") = h;
    std::ostringstream serialized;
    output.save(serialized, "", pugi::format_raw, pugi::encoding_utf8);
    return serialized.str();
}
double kernel(double x) {
    x = std::abs(x);
    if (x <= 1)
        return (1.5 * x - 2.5) * x * x + 1;
    if (x < 2)
        return ((-0.5 * x + 2.5) * x - 4) * x + 2;
    return 0;
}
void sample(const Raster &source, double x, double y, uint8_t *destination) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    double alpha = 0;
    std::array<double, 3> color{};
    for (int j = iy - 1; j <= iy + 2; ++j)
        for (int i = ix - 1; i <= ix + 2; ++i) {
            if (i < 0 || j < 0 || i >= source.width || j >= source.height)
                continue;
            const auto *pixel = source.pixels.data() + (size_t(j) * source.width + i) * 4;
            const double weight = kernel(x - i) * kernel(y - j);
            const double a = pixel[3] / 255.0;
            alpha += weight * a;
            for (int c = 0; c < 3; ++c)
                color[c] += pixel[c] * a * weight;
        }
    for (int c = 0; c < 3; ++c)
        destination[c] = alpha > 1e-8 ? uint8_t(std::clamp(std::round(color[c] / alpha), 0.0, 255.0)) : 0;
    destination[3] = uint8_t(std::clamp(std::round(alpha * 255), 0.0, 255.0));
}
Raster rotate_crop(const Raster &source, const Geometry &g, int maximum = 8192) {
    const auto [cw, ch] = canvas(source.width, source.height, g.angle);
    double ow = std::max(1.0, std::round(cw * g.width)), oh = std::max(1.0, std::round(ch * g.height));
    const double factor = std::min(1.0, maximum / std::max(ow, oh));
    Raster result{std::max(1, int(std::round(ow * factor))), std::max(1, int(std::round(oh * factor))), {}};
    bounds(result.width, result.height);
    result.pixels.resize(size_t(result.width) * result.height * 4);
    const auto rad = g.angle * std::acos(-1.0) / 180.0;
    const auto cosine = std::cos(rad), sine = std::sin(rad);
    for (int y = 0; y < result.height; ++y)
        for (int x = 0; x < result.width; ++x) {
            double dx = (x + .5) * ow / result.width + g.x * cw - cw / 2;
            double dy = (y + .5) * oh / result.height + g.y * ch - ch / 2;
            double sx = cosine * dx + sine * dy, sy = -sine * dx + cosine * dy;
            if (g.flip_x)
                sx = -sx;
            if (g.flip_y)
                sy = -sy;
            sample(source, sx + source.width / 2.0 - .5, sy + source.height / 2.0 - .5,
                   result.pixels.data() + (size_t(y) * result.width + x) * 4);
        }
    return result;
}
void be32(std::string &data, uint32_t n) {
    for (int shift = 24; shift >= 0; shift -= 8)
        data += char(n >> shift);
}
void chunk(std::string &out, const char *type, const std::string &data) {
    be32(out, uint32_t(data.size()));
    const auto start = out.size();
    out.append(type, 4);
    out += data;
    be32(out, uint32_t(mz_crc32(0, reinterpret_cast<const unsigned char *>(out.data() + start), data.size() + 4)));
}
} // namespace
Raster decode(std::string_view bytes, const std::string &extension, int vector_size) {
    if (bytes.size() > 64ull * 1024 * 1024)
        throw Error("size_limit");
    if (folded(extension) == ".svg" || extension == "svg") {
        auto doc = svg_document(bytes);
        const double scale = vector_size / std::max(doc->width(), doc->height());
        auto bitmap = doc->renderToBitmap(std::max(1, int(std::round(doc->width() * scale))),
                                          std::max(1, int(std::round(doc->height() * scale))));
        if (bitmap.isNull())
            throw Error("invalid_image");
        bitmap.convertToRGBA();
        Raster result{bitmap.width(), bitmap.height(), {}};
        bounds(result.width, result.height);
        result.pixels.resize(size_t(result.width) * result.height * 4);
        for (int y = 0; y < result.height; ++y)
            std::memcpy(result.pixels.data() + size_t(y) * result.width * 4,
                        bitmap.data() + size_t(y) * bitmap.stride(), size_t(result.width) * 4);
        return result;
    }
    auto *stream = SDL_RWFromConstMem(bytes.data(), int(bytes.size()));
    if (!stream)
        throw Error("invalid_image");
    SDL_Surface *loaded = IMG_Load_RW(stream, 1);
    if (!loaded)
        throw Error("invalid_image", {{"detail", IMG_GetError()}});
    struct Free {
        SDL_Surface *p;
        ~Free() { SDL_FreeSurface(p); }
    } source{loaded};
    bounds(loaded->w, loaded->h);
    SDL_Surface *converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    if (!converted)
        throw Error("invalid_image");
    Free rgba{converted};
    Raster result{converted->w, converted->h, {}};
    result.pixels.resize(size_t(result.width) * result.height * 4);
    for (int y = 0; y < result.height; ++y)
        std::memcpy(result.pixels.data() + size_t(y) * result.width * 4,
                    static_cast<const uint8_t *>(converted->pixels) + size_t(y) * converted->pitch,
                    size_t(result.width) * 4);
    return result;
}
std::string encode_png(const Raster &image) {
    bounds(image.width, image.height);
    if (image.pixels.size() != size_t(image.width) * image.height * 4)
        throw Error("invalid_image");
    std::string result("\x89PNG\r\n\x1a\n", 8), header;
    be32(header, uint32_t(image.width));
    be32(header, uint32_t(image.height));
    header.append("\x08\x06\0\0\0", 5);
    chunk(result, "IHDR", header);
    std::string scanlines;
    scanlines.reserve((size_t(image.width) * 4 + 1) * image.height);
    for (int y = 0; y < image.height; ++y) {
        scanlines += '\0';
        scanlines.append(reinterpret_cast<const char *>(image.pixels.data() + size_t(y) * image.width * 4),
                         size_t(image.width) * 4);
    }
    mz_ulong size = mz_compressBound(scanlines.size());
    std::string packed(size, '\0');
    if (mz_compress2(reinterpret_cast<unsigned char *>(packed.data()), &size,
                     reinterpret_cast<const unsigned char *>(scanlines.data()), scanlines.size(),
                     MZ_BEST_SPEED) != MZ_OK)
        throw Error("image_encode_failed");
    packed.resize(size);
    chunk(result, "IDAT", packed);
    chunk(result, "IEND", {});
    return result;
}
Result transform(std::string_view original, const std::string &extension, const Json &recipe) {
    Geometry g(recipe);
    Result result;
    result.recipe = g.json();
    if (g.angle == 0 && !g.flip_x && !g.flip_y && g.x == 0 && g.y == 0 && g.width == 1 && g.height == 1) {
        // Import without an edit preserves the exact source pixels and codec.
        auto source = decode(original, extension);
        result.bytes = original;
        result.extension = extension.starts_with('.') ? extension : '.' + extension;
        if (result.extension == ".svg") {
            auto svg = svg_document(original);
            result.width = int(std::round(svg->width()));
            result.height = int(std::round(svg->height()));
        } else {
            result.width = source.width;
            result.height = source.height;
        }
        return result;
    }
    if (folded(extension) == ".svg" || extension == "svg") {
        result.bytes = transform_svg(original, g, result.width, result.height);
        result.extension = ".svg";
    } else {
        auto source = decode(original, extension);
        auto output = rotate_crop(source, g);
        result.width = output.width;
        result.height = output.height;
        result.extension = ".png";
        result.bytes = encode_png(output);
    }
    return result;
}
Raster preview(std::string_view bytes, const std::string &extension, const Json &recipe, int maximum) {
    Geometry g(recipe);
    if (folded(extension) == ".svg" || extension == "svg") {
        int w, h;
        auto svg = transform_svg(bytes, g, w, h);
        return decode(svg, ".svg", maximum);
    }
    return rotate_crop(decode(bytes, extension), g, maximum);
}
std::string base64(std::string_view bytes) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t value = uint8_t(bytes[i]) << 16;
        if (i + 1 < bytes.size())
            value |= uint8_t(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size())
            value |= uint8_t(bytes[i + 2]);
        result += alphabet[(value >> 18) & 63];
        result += alphabet[(value >> 12) & 63];
        result += i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=';
        result += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
    }
    return result;
}
std::string silhouette() {
    return R"(<svg xmlns="http://www.w3.org/2000/svg" width="180" height="180" viewBox="0 0 180 180"><rect width="180" height="180" rx="24" fill="#293c4c"/><circle cx="90" cy="62" r="30" fill="#91acba"/><path d="M26 180v-27c0-35 25-50 64-50s64 15 64 50v27" fill="#91acba"/></svg>)";
}
} // namespace bmh::editor::images
