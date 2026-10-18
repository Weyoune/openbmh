// SPDX-License-Identifier: GPL-3.0-only
#include "formats/le.hpp"
#include "formats/rule_base.hpp"

#include <algorithm>
#include <fstream>
#include <vector>

namespace bmh {

namespace {

struct Object {
    uint32_t vsize, base, flags, page_index, page_count;
};

}  // namespace

std::string LeImage::cstring(uint32_t address) const {
    std::string s;
    while (contains(address) && u8(address) != 0) s.push_back(char(u8(address++)));
    return s;
}

namespace {
constexpr char kImageMagic[14] = "OPENBMH-IMAGE";  // 13 characters and the NUL
}

void save_image(const LeImage& image, const std::filesystem::path& path) {
    Bytes out(kImageMagic, kImageMagic + sizeof kImageMagic);
    for (uint32_t v : {image.base(), image.size()})
        for (int b = 0; b < 4; ++b) out.push_back(uint8_t(v >> (8 * b)));
    out.insert(out.end(), image.bytes().begin(), image.bytes().end());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    if (!file) throw FormatError("cannot write the OpenBMH image");
}

LeImage load_le(const std::filesystem::path& path, bool apply_supported_rules) {
    const Bytes raw = read_file(path);
    if (raw.size() >= sizeof kImageMagic + 8 && std::equal(kImageMagic, kImageMagic + sizeof kImageMagic, raw.begin())) {
        const auto word = [&](size_t at) { return uint32_t(raw[at] | raw[at + 1] << 8 | raw[at + 2] << 16) | uint32_t(raw[at + 3]) << 24; };
        const uint32_t base = word(sizeof kImageMagic), size = word(sizeof kImageMagic + 4);
        if (raw.size() != sizeof kImageMagic + 8 + size_t(size)) throw FormatError("OpenBMH image: wrong size");
        LeImage img = LeImage::from_bytes(base, Bytes(raw.begin() + sizeof kImageMagic + 8, raw.end()));
        if (apply_supported_rules) apply_rule_base(img);
        return img;
    }
    auto u8 = [&](size_t o) -> uint32_t {
        if (o >= raw.size()) throw FormatError("LE: read beyond the file");
        return raw[o];
    };
    auto u16 = [&](size_t o) { return u8(o) | u8(o + 1) << 8; };
    auto u32 = [&](size_t o) { return u16(o) | uint32_t(u16(o + 2)) << 16; };
    const size_t le = u32(0x3c);
    if (u16(le) != 0x454c) throw FormatError("LE: no LE header");
    auto h = [&](size_t off) { return u32(le + off); };
    const uint32_t pages = h(0x14), page_size = h(0x28), last_page = h(0x2c);
    const size_t object_table = le + h(0x40), page_map = le + h(0x48);
    const size_t fixup_pages = le + h(0x68), fixup_records = le + h(0x6c);
    const uint32_t objects = h(0x44), data_pages = h(0x80);

    std::vector<Object> obj;
    uint32_t lo = 0xFFFFFFFFu, hi = 0;
    for (uint32_t i = 0; i < objects; i++) {
        const size_t o = object_table + i * 24;
        Object ob{u32(o), u32(o + 4), u32(o + 8), u32(o + 12), u32(o + 16)};
        obj.push_back(ob);
        const uint32_t span = std::max(ob.vsize, ob.page_count * page_size);
        lo = std::min(lo, ob.base);
        hi = std::max(hi, ob.base + (span + page_size - 1) / page_size * page_size);
    }
    LeImage img;
    img.base_ = lo;
    img.bytes_.assign(hi - lo, 0);
    std::vector<uint32_t> page_address(pages + 1, 0);
    for (const Object& ob : obj)
        for (uint32_t p = 0; p < ob.page_count; p++) {
            const uint32_t logical = ob.page_index + p;
            const size_t e = page_map + (logical - 1) * 4;
            const uint32_t file_page = u8(e) << 16 | u8(e + 1) << 8 | u8(e + 2);
            const uint32_t va = ob.base + p * page_size;
            if (logical <= pages) page_address[logical] = va;
            if (u8(e + 3) != 0) continue;  // not a loaded page
            size_t size = file_page == pages ? last_page : page_size;
            const size_t src = data_pages + size_t(file_page - 1) * page_size;
            size = std::min(size, raw.size() - src);
            std::copy(raw.begin() + long(src), raw.begin() + long(src + size), img.bytes_.begin() + (va - lo));
        }
    auto put16 = [&](size_t pos, uint32_t v) {
        img.bytes_[pos] = uint8_t(v), img.bytes_[pos + 1] = uint8_t(v >> 8);
    };
    auto put32 = [&](size_t pos, uint32_t v) { put16(pos, v), put16(pos + 2, v >> 16); };
    for (uint32_t pg = 1; pg <= pages; pg++) {
        size_t r = fixup_records + u32(fixup_pages + (pg - 1) * 4);
        const size_t end = fixup_records + u32(fixup_pages + pg * 4);
        while (r < end) {
            const uint32_t src = u8(r), flags = u8(r + 1);
            r += 2;
            const uint32_t type = src & 0x0f;
            const bool list = (src & 0x20) != 0;
            uint32_t count = 1;
            int16_t source_offset = 0;
            if (list) count = u8(r++);
            else source_offset = int16_t(u16(r)), r += 2;
            if (flags & 3) throw FormatError("LE: external fixup");
            uint32_t target_object;
            if (flags & 0x40) target_object = u16(r), r += 2;
            else target_object = u8(r++);
            uint32_t target_offset = 0;
            if (type != 0x02) {
                if (flags & 0x10) target_offset = u32(r), r += 4;
                else target_offset = u16(r), r += 2;
            }
            std::vector<int16_t> offsets;
            if (list)
                for (uint32_t k = 0; k < count; k++) offsets.push_back(int16_t(u16(r))), r += 2;
            else
                offsets.push_back(source_offset);
            const uint32_t target = obj.at(target_object - 1).base + target_offset;
            for (int16_t so : offsets) {
                const uint32_t sa = page_address[pg] + uint32_t(int32_t(so));
                const int64_t pos = int64_t(sa) - lo;
                if (pos < 0 || pos + 4 > int64_t(img.bytes_.size())) continue;
                switch (type) {
                    case 0x07: put32(size_t(pos), target); break;
                    case 0x08: put32(size_t(pos), target - (sa + 4)); break;
                    case 0x05: put16(size_t(pos), target); break;
                    case 0x02: put16(size_t(pos), target_object); break;
                    case 0x06: put32(size_t(pos), target), put16(size_t(pos) + 4, target_object); break;
                    default: break;
                }
            }
        }
    }
    if (apply_supported_rules) apply_rule_base(img);
    return img;
}

}  // namespace bmh
