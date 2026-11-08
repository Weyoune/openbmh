"""Finds the game texts in a German-layout image (a Diamond BMH.exe, or the translated English FLTD.EXE).

generate.py keeps the numbers of the image in the source files (formats/*_constants.inc) and puts the game texts into
the data file openbmh.dat that ships with the release. A range counts as text when it is

  * a NUL terminated string that a pointer variable of the data object (or of the English data segment) points to, or
  * a string that a 32 bit immediate of the code points to (a "push offset text"), or
  * a long printable run with a space (a sentence), or
  * a string the program read as a string while a trace was recorded (OPENBMH_LE_TRACE, kind 2).

Letter permutation tables of the cup draw ('abcdefghijklmnopqr', all letters different) and file names are numbers /
names, not texts, and stay in the constants. Used by generate.py.
"""
import re
import struct

PRINTABLE = set(range(0x20, 0x7F))
UMLAUTS = {0x84, 0x94, 0x81, 0x8E, 0x99, 0x9A, 0xE1}        # CP437 letters seen in character sets
TEXT_BYTES = PRINTABLE | UMLAUTS
FILE_NAME = re.compile(r"[A-Za-z0-9_./\\*?-]*[./\\][A-Za-z0-9_./\\*?-]*")


def string_end(data, base, start):
    """Address after the NUL of the string starting at `start`, or None when it is not a clean string."""
    i = start - base
    n = 0
    while i < len(data) and data[i] != 0:
        if data[i] not in TEXT_BYTES and data[i] < 0x20:
            return None
        i += 1
        n += 1
        if n > 400:
            return None
    return i + 1 + base if i < len(data) else None


def is_permutation(text):
    letters = text.lower()
    return letters.isalpha() and len(set(letters)) == len(letters) and len(letters) in (4, 6, 10, 12, 16, 18, 20)


def ascending(text, n=4):
    """True when the text holds n or more ascending consecutive characters (a character set 'abcdef', not a word)."""
    run = 1
    for a, b in zip(text, text[1:]):
        run = run + 1 if ord(b) == ord(a) + 1 else 1
        if run >= n:
            return True
    return False


def is_text(text):
    if not re.search(r"[A-Za-z]{3}", text) or is_permutation(text) or text.startswith(("f\\", "Ã")) or FILE_NAME.fullmatch(text):
        return False
    # a single token in one case is a name or a table fragment (BLASTER, KRON_FBM, V3.7, 'njpmd' from a fixture row), not a
    # sentence or a label of the game; letter and character-set tables are not texts either
    if " " not in text and (text.isupper() or text.islower() or re.fullmatch(r"[A-Z][A-Za-z0-9_.]*\d[A-Za-z0-9_.]*", text)):
        return False
    if ascending(text) or sum(c.isalpha() or c == " " for c in text) < 0.6 * len(text):
        return False
    return True


def text_ranges(data, base, targets, sources, code, layout_pointers, traced_strings):
    """data: the image bytes at address `base`. targets: [(lo, hi)] where texts may lie, sources: [(lo, hi)] where
    pointer variables lie, code: [(lo, hi)] where immediates are searched. Returns sorted (start, length) incl. the NUL."""
    found = {}

    def in_targets(address):
        return any(lo <= address < hi for lo, hi in targets)

    def add(start, end):
        if in_targets(start) and end > start:
            found[start] = max(found.get(start, 0), end - start)

    def text_at(value):
        end = string_end(data, base, value)
        if end and end - value >= 4:
            text = bytes(data[value - base:end - 1 - base]).decode("latin1")
            if is_text(text):
                return end
        return None

    # 1. strings that pointer variables point to
    for lo, hi in sources:
        for at in range(lo, hi - 3, 4):
            value = struct.unpack_from("<I", data, at - base)[0]
            if in_targets(value) and value != at:
                end = text_at(value)
                if end:
                    add(value, end)
    for pointer in layout_pointers:
        if pointer - base < 0 or pointer - base + 4 > len(data):
            continue
        value = struct.unpack_from("<I", data, pointer - base)[0]
        if in_targets(value):
            end = string_end(data, base, value)
            if end:
                add(value, end)
    # 1b. strings the code points to: after the fixups a "push offset text" holds the address as a 32 bit immediate
    for lo, hi in code:
        for at in range(lo, hi - 3):
            value = struct.unpack_from("<I", data, at - base)[0]
            if in_targets(value) and data[value - 1 - base] == 0:
                end = text_at(value)
                if end:
                    add(value, end)
    # 2. long printable runs with a space (sentences)
    for lo, hi in targets:
        i = lo - base
        while i < hi - base:
            if data[i] in PRINTABLE:
                j = i
                while j < hi - base and data[j] in TEXT_BYTES:
                    j += 1
                text = bytes(data[i:j]).decode("latin1")
                if j - i >= 8 and " " in text and re.search(r"[A-Za-z]{3}", text):
                    add(i + base, j + base + (1 if j < len(data) and data[j] == 0 else 0))
                i = j
            else:
                i += 1
    # 3. strings the program read while a trace was recorded
    for start, length in traced_strings:
        text = bytes(data[start - base:start - base + max(length - 1, 0)]).decode("latin1")
        if is_text(text):
            add(start, start + length)
    merged = []
    for start in sorted(found):
        end = start + found[start]
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return [(s, e - s) for s, e in merged]
