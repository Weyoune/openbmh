#!/usr/bin/env python3
"""Generates the embedded game images of OpenBMH, so that the game runs from the asset files alone (no BMH.exe, no
FLTD.EXE): src/formats/diamond_constants.inc (German, Diamond 3.7) and src/formats/fltd_constants.inc (English
Football Limited). Both hold every non-zero byte of the data the engine reads - tables, rates, positions, prices and the
game texts - as runs of bytes; zero runs are not stored.

German (a Diamond 3.7 BMH.exe, read here and never copied into the repository):
  python generate.py german --exe BMH.exe --trace trace1.txt [--trace trace2.txt ...] --out ../../src/formats/diamond_constants.inc
  The file backed part of the data object, plus the few bytes of the code object that the program reads as numbers or
  tables (instruction immediates such as the calendar setup). The code bytes come from the read traces of OPENBMH_LE_TRACE
  (formats/le.hpp: run the data dependent tests and scenarios with the variable set to a file; the lines are
  "<flag> <start> <length>" in hex, flag 1 = number read, 2 = string read, 4 = start of a bulk read) and from a window
  at every layout constant that names a place in the code objects.

English (the German layout image of FLTD.EXE, the cached "BMH.exe" of an English game that translate_fltd() made):
  python generate.py english --image BMH.exe --out ../../src/formats/fltd_constants.inc
  Every non-zero byte of the image.
"""
import argparse
import re
import struct
from pathlib import Path

DATA_LO, DATA_PAGES = 0xA0000, 12          # the initialised pages of the data object (file backed)
DATA_HI = DATA_LO + DATA_PAGES * 4096
CODE = (0x10000, 0x10000 + 438272)
PATCH = (0x80000, 0x81000)


def load_le(path):
    """The loaded image of a DOS/4GW LE executable (objects at their link addresses, internal fixups applied)."""
    raw = Path(path).read_bytes()
    u16 = lambda o: struct.unpack_from("<H", raw, o)[0]
    u32 = lambda o: struct.unpack_from("<I", raw, o)[0]
    le = u32(0x3C)
    if u16(le) != 0x454C:
        raise SystemExit("no LE header")
    h = lambda off: u32(le + off)
    pages, page_size, last_page = h(0x14), h(0x28), h(0x2C)
    object_table, page_map = le + h(0x40), le + h(0x48)
    fixup_pages, fixup_records = le + h(0x68), le + h(0x6C)
    objects, data_pages = h(0x44), h(0x80)
    obj, lo, hi = [], 0xFFFFFFFF, 0
    for i in range(objects):
        o = object_table + i * 24
        ob = (u32(o), u32(o + 4), u32(o + 8), u32(o + 12), u32(o + 16))   # vsize, base, flags, page index, pages
        obj.append(ob)
        span = max(ob[0], ob[4] * page_size)
        lo, hi = min(lo, ob[1]), max(hi, ob[1] + (span + page_size - 1) // page_size * page_size)
    data = bytearray(hi - lo)
    page_address = [0] * (pages + 1)
    for vsize, base, flags, index, count in obj:
        for p in range(count):
            logical = index + p
            e = page_map + (logical - 1) * 4
            file_page = raw[e] << 16 | raw[e + 1] << 8 | raw[e + 2]
            va = base + p * page_size
            if logical <= pages:
                page_address[logical] = va
            if raw[e + 3] != 0:
                continue
            size = last_page if file_page == pages else page_size
            src = data_pages + (file_page - 1) * page_size
            size = min(size, len(raw) - src)
            data[va - lo:va - lo + size] = raw[src:src + size]

    def put(pos, fmt, v):
        struct.pack_into(fmt, data, pos, v & ((1 << (8 * struct.calcsize(fmt))) - 1))

    for pg in range(1, pages + 1):
        r = fixup_records + u32(fixup_pages + (pg - 1) * 4)
        end = fixup_records + u32(fixup_pages + pg * 4)
        while r < end:
            src, flags = raw[r], raw[r + 1]
            r += 2
            ftype, is_list = src & 0x0F, bool(src & 0x20)
            count, source_offset = 1, 0
            if is_list:
                count = raw[r]; r += 1
            else:
                source_offset = struct.unpack_from("<h", raw, r)[0]; r += 2
            if flags & 3:
                raise SystemExit("external fixup")
            if flags & 0x40:
                target_object = u16(r); r += 2
            else:
                target_object = raw[r]; r += 1
            target_offset = 0
            if ftype != 0x02:
                if flags & 0x10:
                    target_offset = u32(r); r += 4
                else:
                    target_offset = u16(r); r += 2
            offsets = []
            if is_list:
                for _ in range(count):
                    offsets.append(struct.unpack_from("<h", raw, r)[0]); r += 2
            else:
                offsets.append(source_offset)
            target = obj[target_object - 1][1] + target_offset
            for so in offsets:
                sa = page_address[pg] + so
                pos = sa - lo
                if pos < 0 or pos + 4 > len(data):
                    continue
                if ftype == 0x07: put(pos, "<I", target)
                elif ftype == 0x08: put(pos, "<I", target - (sa + 4))
                elif ftype == 0x05: put(pos, "<H", target)
                elif ftype == 0x02: put(pos, "<H", target_object)
                elif ftype == 0x06: put(pos, "<I", target); put(pos + 4, "<H", target_object)
    return lo, data


def load_openbmh_image(path):
    raw = Path(path).read_bytes()
    if raw[:14] != b"OPENBMH-IMAGE\0":
        raise SystemExit("not an OPENBMH-IMAGE file")
    base, size = struct.unpack_from("<II", raw, 14)
    data = raw[22:]
    if len(data) != size:
        raise SystemExit("OPENBMH-IMAGE: wrong size")
    return base, bytearray(data)


def read_traces(paths):
    numbers, bulk = [], []
    for path in paths:
        for line in Path(path).read_text().splitlines():
            flag, start, length = (int(x, 16) for x in line.split())
            if flag == 1:
                numbers.append((start, length))
            elif flag == 4:
                bulk.append((start, length))
    return numbers, bulk


def merge(ranges, gap=0):
    out = []
    for start, end in sorted((s, s + l) for s, l in ranges):
        if out and start <= out[-1][1] + gap:
            out[-1][1] = max(out[-1][1], end)
        else:
            out.append([start, end])
    return out


def nonzero_runs(data, base, lo, hi, gap=8):
    """[start, end) runs that hold every non-zero byte of lo..hi; zero gaps up to `gap` bytes stay inside a run."""
    runs, cur = [], None
    for address in range(lo, hi):
        if data[address - base]:
            if cur and address - cur[1] <= gap:
                cur[1] = address + 1
            else:
                cur = [address, address + 1]
                runs.append(cur)
    return runs


def write_inc(path, title, base, data, chunks):
    out = ["// SPDX-License-Identifier: GPL-3.0-only",
           f"// Generated by tools/diamond_constants/generate.py from {title} - do not edit.",
           "// Every non-zero byte of the data the engine reads (tables, rates, positions, prices, game texts), as runs.",
           "// Included inside a namespace by formats/diamond_image.cpp.",
           f"inline constexpr uint32_t kImageBase = {base}, kImageSize = {len(data)};"]
    total = 0
    for i, (start, end) in enumerate(chunks):
        body = data[start - base:end - base]
        total += len(body)
        rows = [", ".join(f"0x{b:02X}" for b in body[k:k + 24]) for k in range(0, len(body), 24)]
        out.append(f"inline constexpr uint8_t kChunk{i}[] = {{\n    " + ",\n    ".join(rows) + "};")
    out.append("inline constexpr Chunk kChunks[] = {")
    for i, (start, end) in enumerate(chunks):
        out.append(f"    {{{start}, {end - start}, kChunk{i}}},")
    out.append("};")
    Path(path).write_text("\n".join(out) + "\n")
    print(f"{len(chunks)} chunks, {total} bytes")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="mode", required=True)
    de = sub.add_parser("german")
    de.add_argument("--exe", required=True)
    de.add_argument("--trace", action="append", default=[])
    de.add_argument("--layout", default=str(Path(__file__).parents[2] / "src/formats/executable_layout.hpp"))
    de.add_argument("--out", required=True)
    en = sub.add_parser("english")
    en.add_argument("--image", required=True)
    en.add_argument("--out", required=True)
    args = ap.parse_args()

    if args.mode == "english":
        base, data = load_openbmh_image(args.image)
        write_inc(args.out, "the German layout image of FLTD.EXE (Football Limited)", base, data,
                  nonzero_runs(data, base, base, base + len(data)))
        return

    base, data = load_le(args.exe)
    numbers, bulk = read_traces(args.trace)
    layout = Path(args.layout).read_text()
    # numbers read from the code or patch object: the bytes read, a margin, and a window after bulk pointers
    wanted = []
    for s, l in numbers:
        if CODE[0] <= s < CODE[1] or PATCH[0] <= s < PATCH[1]:
            wanted.append((max(s - 4, 0), l + 8))
    for s, l in bulk:
        if CODE[0] <= s < CODE[1] or PATCH[0] <= s < PATCH[1]:
            wanted.append((s, 256))
    # and, whatever the traces saw, a window at every layout constant that names a place in the code objects
    for name, address in re.findall(r"inline constexpr auto (\w+) = 0x([0-9a-fA-F]+);", layout):
        a = int(address, 16)
        if CODE[0] <= a < CODE[1] or PATCH[0] <= a < PATCH[1]:
            wanted.append((a, 16))
    chunks = sorted(nonzero_runs(data, base, DATA_LO, DATA_HI) + merge(wanted, 16))
    write_inc(args.out, "BMH 2026-27 Diamond edition 3.7", base, data, chunks)


if __name__ == "__main__":
    main()
