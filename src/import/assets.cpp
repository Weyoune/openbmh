// SPDX-License-Identifier: GPL-3.0-only
#include "import/assets.hpp"
#include "import/detail.hpp"
#include "formats/executable_layout.hpp"
#include "launch/game_data.hpp"
#include "launch/archive.hpp"
#include "audio/container.hpp"
#include <algorithm>
#include <cctype>
#include <limits>

namespace bmh::obw {
namespace {
using namespace detail;
namespace fs = std::filesystem;

void be32(std::string& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out += char(value >> shift);
}
void chunk(std::string& out, const char* name, const std::string& payload) {
    be32(out, uint32_t(payload.size()));
    const auto start = out.size();
    out.append(name, 4);
    out += payload;
    be32(out, crc(out.substr(start)));
}
std::string png(const Image& image, const Palette& palette, bool transparent) {
    std::string out("\x89PNG\r\n\x1a\n", 8), header;
    be32(header, uint32_t(image.width)); be32(header, uint32_t(image.height));
    header.append("\x08\x03\x00\x00\x00", 5); // indexed PNG, exact palette indices
    chunk(out, "IHDR", header);
    std::string colors;
    for (const uint32_t color : palette.argb)
        for (int shift : {16, 8, 0}) colors += char(color >> shift);
    chunk(out, "PLTE", colors);
    if (transparent) chunk(out, "tRNS", std::string(1, '\0'));
    std::string rows;
    for (int y = 0; y < image.height; ++y) {
        rows += '\0';
        rows.append(reinterpret_cast<const char*>(image.pixels.data() + size_t(y) * image.width), size_t(image.width));
    }
    mz_ulong size = mz_compressBound(static_cast<mz_ulong>(rows.size()));
    std::string compressed(size, '\0');
    if (mz_compress2(reinterpret_cast<unsigned char*>(compressed.data()), &size,
        reinterpret_cast<const unsigned char*>(rows.data()), static_cast<mz_ulong>(rows.size()), MZ_BEST_COMPRESSION) != MZ_OK)
        fail("cannot compress PNG");
    compressed.resize(size);
    chunk(out, "IDAT", compressed); chunk(out, "IEND", "");
    return out;
}
fs::path required(const fs::path& root, const std::string& name) {
    const auto path = launch::find_file(root, name);
    if (!path || !fs::is_regular_file(*path) || fs::is_symlink(fs::symlink_status(*path))) fail("missing regular resource: " + name);
    return *path;
}
std::string lower(std::string value) {
    for (char& c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
bool numeric(const std::string& name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
}
// Source sprites use a linear packed stride; a feature may cross a row edge.
Image extract(const Image& atlas, int sx, int sy, int width, int height) {
    if (sx < 0 || sy < 0 || width <= 0 || height <= 0 || width > 1024 || height > 1024) fail("invalid sprite geometry");
    Image out(width, height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto at = size_t(sy + y) * size_t(atlas.width) + size_t(sx + x);
        if (at >= atlas.pixels.size()) fail("sprite exceeds its packed atlas");
        out.at(x, y) = atlas.pixels[at];
    }
    return out;
}
J asset(const std::string& key, const std::string& path, const std::string& source, const std::string& kind) {
    return O{{"key", key}, {"files", O{{"native", path}}}, {"source", source},
        {"author", "Original installation rights holders"}, {"license", "local-import-only"}, {"kind", kind}};
}
void image_entry(Documents& out, A& resources, A& index, const std::string& key,
                 const std::string& name, const Image& image, const Palette& palette, bool transparent = false) {
    const auto base = "assets/classic/images/" + name;
    out[base + ".png"] = png(image, palette, transparent);
    out[base + ".pixels"] = binary(image.pixels);
    resources.emplace_back(O{{"name", name}, {"width", image.width}, {"height", image.height},
        {"pixels", base + ".pixels"}, {"png", base + ".png"}});
    index.emplace_back(asset(key, base + ".png", name, "indexed-image"));
}
} // namespace

cli::Json portrait_recipe(const std::string& legacy_name, const LeImage& exe) {
    using namespace detail;
    // Unsigned overflow and signed source characters deliberately match the UI.
    uint32_t seed = 0x14344;
    for (size_t i = 0; i < legacy_name.size(); ++i) seed += int(int8_t(legacy_name[i])) * uint32_t(i) * 10;
    const auto next = [&](uint32_t maximum) {
        seed = (seed * 16 + 0xa7) % 4000000;
        return int(seed * (maximum + 1) / 4000000);
    };
    next(100);
    const int background = next(3);
    A layers;
    for (uint32_t layer = 0; layer < 5; ++layer) {
        const auto count = exe.u8(layout::ClubPlayerDetail_ClubPlayerDetailRedraw_RuntimeField2 + layer);
        if (!count) fail("portrait layer has no parts");
        layers.emplace_back(next(uint32_t(count - 1)));
    }
    return O{{"set", "bmh-classic@1"}, {"background", background}, {"layers", std::move(layers)}};
}

void import_assets(Documents& out, const std::filesystem::path& root, const Database& db, const LeImage& exe) {
    using namespace detail;
    const auto palette = load_palette(required(root, "M3PC.COL"));
    A colors, index, images, fonts, audio, animations;
    for (const auto color : palette.argb) colors.emplace_back(int64_t(color));
    put(out, "assets/classic/palette.json", O{{"schema", "classic-palette@1.0"}, {"argb", std::move(colors)}});
    std::map<std::string, Image> atlases;
    for (const char* group : {"KB", "W"}) {
        const auto folder = launch::find_file(root, group);
        if (!folder || !fs::is_directory(*folder)) fail(std::string("missing directory: ") + group);
        std::vector<fs::path> paths;
        for (const auto& entry : fs::directory_iterator(*folder))
            if (entry.is_regular_file() && lower(launch::path_to_utf8(entry.path().extension())) == ".vga" &&
                numeric(launch::path_to_utf8(entry.path().stem()))) paths.push_back(entry.path());
        std::sort(paths.begin(), paths.end());
        for (const auto& path : paths) {
            if (fs::is_symlink(fs::symlink_status(path)) || fs::file_size(path) > 16 * 1024 * 1024) fail("unsafe image input");
            const auto bytes = read_file(path);
            if (bytes.size() < 8 || le16(bytes, 0) == 0 || le16(bytes, 2) == 0 ||
                uint64_t(le16(bytes, 0)) * le16(bytes, 2) > 16 * 1024 * 1024) fail("invalid image dimensions");
            const auto name = lower(group) + '/' + launch::path_to_utf8(path.stem());
            if (atlases.contains(name)) fail("duplicate image resource: " + name);
            auto image = decode_vga(bytes);
            image_entry(out, images, index, "asset:classic/" + name, name, image, palette);
            atlases.emplace(name, std::move(image));
        }
    }
    // Crests reference their original database identity, independently of runtime draws.
    auto clubs = items(out, "clubs");
    for (auto& club : clubs) {
        const auto team = size_t(num(get(get(club, "x-legacy"), "team_index")));
        const int selector = le16(db.raw_club_badge_selectors, team * 2) & 511;
        const std::string sheet = "w/" + std::to_string(selector >> 3);
        const bool neutral = (team >= 90 && team < 138) || (team >= 168 && team < 357);
        const std::string atlas_name = neutral ? "kb/0" : sheet;
        if (!atlases.contains(atlas_name)) fail("missing crest sheet " + atlas_name);
        const auto name = "crests/" + std::to_string(team);
        const auto key = "asset:club/" + str(get(club, "id")).substr(str(get(club, "id")).find(':') + 1) + "/crest";
        image_entry(out, images, index, key, name, extract(atlases.at(atlas_name), neutral ? 8 : (selector & 7) * 38, 0, 38, 38), palette, true);
        (*club.object())["crest"] = key;
    }
    table(out, "clubs", std::move(clubs));

    const auto portrait = atlases.find("kb/29");
    if (portrait == atlases.end()) fail("missing player portrait atlas KB/29.VGA");
    A backgrounds, layers;
    for (int i = 0; i < 4; ++i) {
        const auto name = "portraits/background-" + std::to_string(i);
        const auto key = "asset:portrait/bmh-classic/background/" + std::to_string(i);
        image_entry(out, images, index, key, name, extract(portrait->second, 108 + i * 38, 168, 38, 38), palette);
        backgrounds.emplace_back(O{{"asset", key}, {"x", 0}, {"y", 0}});
    }
    for (uint32_t layer = 0; layer < 5; ++layer) {
        int sx = exe.u8(layout::ClubPlayerDetail_DrawPortrait_RuntimeField + layer);
        int sy = exe.u8(layout::ClubPlayerDetail_DrawPortrait_RuntimeField2 + layer);
        const int count = exe.u8(layout::ClubPlayerDetail_ClubPlayerDetailRedraw_RuntimeField2 + layer);
        const int width = exe.u8(layout::ClubPlayerDetail_DrawPortrait_RuntimeField3 + layer);
        const int height = exe.u8(layout::ClubPlayerDetail_DrawPortrait_RuntimeField4 + layer);
        if (!count) fail("empty portrait layer");
        A parts;
        for (int part = 0; part < count; ++part) {
            const auto name = "portraits/layer-" + std::to_string(layer) + '-' + std::to_string(part);
            const auto key = "asset:portrait/bmh-classic/layer/" + std::to_string(layer) + '/' + std::to_string(part);
            image_entry(out, images, index, key, name, extract(portrait->second, sx, sy, width, height), palette, true);
            parts.emplace_back(key);
            sx += width;
            if (sx > exe.u16(layout::ClubPlayerDetail_DrawPortrait_RuntimeField7 + layer * 2)) {
                sx = exe.u8(layout::ClubPlayerDetail_DrawPortrait_RuntimeField + layer); sy += height;
            }
        }
        // Destination relative to the 38x38 background at (181,109) in the legacy card.
        layers.emplace_back(O{{"x", 1 + int(exe.u8(layout::ClubPlayerDetail_DrawPortrait_RuntimeField5 + layer))},
            {"y", 5 + int(exe.u8(layout::ClubPlayerDetail_DrawPortrait_RuntimeField6 + layer))}, {"parts", std::move(parts)}});
    }
    put(out, "assets/portrait-sets/bmh-classic@1/set.json", O{{"schema", "portrait-set@1.0"},
        {"id", "bmh-classic@1"}, {"width", 38}, {"height", 38}, {"transparent_index", 0},
        {"backgrounds", std::move(backgrounds)}, {"layers", std::move(layers)}});

    const auto font_dir = launch::find_file(root, "F");
    if (!font_dir) fail("missing F directory");
    for (const char* source : {"BMH2.FNT", "BMH3.FNT"}) {
        const auto font = load_font(required(*font_dir, source));
        const auto name = lower(source);
        A glyphs;
        for (const auto& glyph : font.glyphs) glyphs.emplace_back(O{{"bit_offset", int(glyph.bit_offset)},
            {"bit_width", int(glyph.bit_width)}, {"advance", int(glyph.advance)}});
        const auto bitmap = "assets/classic/fonts/" + name + ".bitmap";
        out[bitmap] = binary(font.bitmap);
        fonts.emplace_back(O{{"name", name}, {"height", font.height}, {"baseline", font.baseline},
            {"nominal_width", font.nominal_width}, {"flags", int(font.flags)}, {"lo_char", int(font.lo_char)},
            {"hi_char", int(font.hi_char)}, {"modulo", font.modulo}, {"glyphs", std::move(glyphs)}, {"bitmap", bitmap}});
        index.emplace_back(asset("asset:font/classic/" + name, bitmap, std::string("F/") + source, "bitmap-font"));
    }
    const auto sound_dir = launch::find_file(root, "S");
    if (!sound_dir) fail("missing S directory");
    for (int i = 2000; i <= 2003; ++i) {
        const auto name = std::to_string(i);
        const auto bank = read_file(required(*sound_dir, name));
        openbmh::audio::Container parsed;
        std::string error;
        if (!openbmh::audio::parse_container(bank, parsed, error)) fail("audio bank " + name + ": " + error);
        const auto path = "assets/classic/audio/" + name + ".bank";
        out[path] = binary(bank);
        A chunks;
        for (const auto& c : parsed.chunks) chunks.emplace_back(O{{"id", c.id}, {"offset", int64_t(c.payload_offset)}, {"size", int64_t(c.payload_size)}});
        audio.emplace_back(O{{"name", name}, {"path", path}, {"codec", "original-alptraum@1"}, {"chunks", std::move(chunks)}});
        index.emplace_back(asset("asset:audio/classic/" + name, path, "S/" + name, "audio-bank"));
    }
    // Preserve optional animation/editor artwork, but never personal options or saves.
    for (const auto& entry : fs::directory_iterator(root)) {
        const auto name = lower(launch::path_to_utf8(entry.path().filename()));
        const auto ext = lower(launch::path_to_utf8(entry.path().extension()));
        if (!entry.is_regular_file() || (ext != ".fli" && ext != ".flc" && name != "wappen.lbm")) continue;
        if (entry.is_symlink() || entry.file_size() > 32 * 1024 * 1024) fail("unsafe optional artwork");
        const auto path = "assets/classic/optional/" + name;
        out[path] = binary(read_file(entry.path()));
        animations.emplace_back(O{{"name", name}, {"path", path}});
        index.emplace_back(asset("asset:classic/optional/" + name, path, name, "legacy-artwork"));
    }
    put(out, "assets/classic/resources.json", O{{"schema", "classic-resources@1.0"},
        {"images", std::move(images)}, {"fonts", std::move(fonts)}, {"audio", std::move(audio)}, {"optional", std::move(animations)}});
    put(out, "assets/index.json", O{{"schema", "asset-index@1.0"}, {"items", std::move(index)}});
}

void load_assets(const Documents& out, ClassicData& data) {
    using namespace detail;
    const auto palette = parse(out, "assets/classic/palette.json");
    const auto& colors = arr(get(palette, "argb"));
    if (colors.size() != 256) fail("palette must contain 256 colors");
    for (size_t i = 0; i < 256; ++i) {
        const auto color = num(colors[i]);
        if (color < 0 || color > UINT32_MAX) fail("invalid palette color");
        data.palette.argb[i] = uint32_t(color);
    }
    const auto resources = parse(out, "assets/classic/resources.json");
    for (const auto& row : arr(get(resources, "images"))) {
        const auto width = num(get(row, "width")), height = num(get(row, "height"));
        const auto& pixels = out.at(str(get(row, "pixels")));
        if (width <= 0 || height <= 0 || width > 65535 || height > 65535 || width * height != int64_t(pixels.size())) fail("image pixel count disagrees with dimensions");
        Image image(int(width), int(height), 0);
        image.pixels = bytes(pixels);
        if (!data.images.emplace(str(get(row, "name")), std::move(image)).second) fail("duplicate indexed image");
    }
    for (const auto& row : arr(get(resources, "fonts"))) {
        Font font;
        font.height = int(num(get(row, "height"))); font.baseline = int(num(get(row, "baseline")));
        font.nominal_width = int(num(get(row, "nominal_width"))); font.flags = uint8_t(num(get(row, "flags")));
        font.lo_char = uint8_t(num(get(row, "lo_char"))); font.hi_char = uint8_t(num(get(row, "hi_char")));
        font.modulo = int(num(get(row, "modulo"))); font.bitmap = bytes(out.at(str(get(row, "bitmap"))));
        if (font.height <= 0 || font.modulo <= 0 || int64_t(font.height) * font.modulo != int64_t(font.bitmap.size())) fail("invalid font bitmap");
        for (const auto& glyph : arr(get(row, "glyphs"))) {
            const auto offset = num(get(glyph, "bit_offset")), width = num(get(glyph, "bit_width")), advance = num(get(glyph, "advance"));
            // BMH2's zero-width replacement glyph points beyond the bitmap;
            // it never dereferences that offset when drawing.
            if (offset < 0 || offset > UINT16_MAX || width < 0 || width > UINT16_MAX ||
                (width && offset + width > int64_t(font.modulo) * 8) || advance < INT16_MIN || advance > INT16_MAX) fail("invalid font glyph");
            font.glyphs.push_back({uint16_t(offset), uint16_t(width), int16_t(advance)});
        }
        if (font.glyphs.size() != size_t(int(font.hi_char) - font.lo_char + 2)) fail("invalid font glyph count");
        if (!data.fonts.emplace(str(get(row, "name")), std::move(font)).second) fail("duplicate font");
    }
    for (const auto& row : arr(get(resources, "audio"))) {
        auto bank = bytes(out.at(str(get(row, "path"))));
        openbmh::audio::Container parsed;
        std::string error;
        if (!openbmh::audio::parse_container(bank, parsed, error)) fail("invalid audio container: " + error);
        if (!data.audio.emplace(str(get(row, "name")), std::move(bank)).second) fail("duplicate audio bank");
    }
    if (!data.images.contains("kb/29") || data.fonts.size() != 2 || data.audio.size() != 4) fail("incomplete classic media");
}
} // namespace bmh::obw
