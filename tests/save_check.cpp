// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// save_check - loads savegames, writes them again (game/save.hpp) and compares: the decrypted
// stream block by block and the header, then the whole file with the original's ids and
// key put in.
//
// Usage: save_check <savegame.FBM>...
#include <cstdio>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/national.hpp"
#include "game/save.hpp"

using namespace bmh;

int main(int argc, char** argv) {
    int bad = 0;
    for (int a = 1; a < argc; a++) {
        try {
            const Savegame sg = load_savegame(argv[a]);
            const SaveState st(sg.stream);
            GameState g = GameState::from_save(st);
            national_coach_after_load(g);  // (the live form: a Bundestrainer takes over again)
            // (which sets format field = format field: the file keeps the value from before)
            if (g.memory.has(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3)) g.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3, st.data(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3)[0]);
            const Bytes ours = save_stream(g);
            int diff_blocks = 0;
            if (ours.size() != sg.stream.size()) std::printf("%s: stream size %zu / %zu\n", argv[a], ours.size(), sg.stream.size());
            for (const SaveBlock& b : st.blocks()) {
                size_t first = b.size;
                for (size_t i = 0; i < b.size && b.offset + i < ours.size(); i++)
                    if (ours[b.offset + i] != sg.stream[b.offset + i]) {
                        first = i;
                        break;
                    }
                if (first < b.size) {
                    if (diff_blocks++ < 8)
                        std::printf("%s: block %05x%s differs at +0x%zx (%02x / %02x)\n", argv[a], b.address, b.heap ? " (heap)" : "",
                                    first, ours[b.offset + first], sg.stream[b.offset + first]);
                }
            }
            bool tail = ours.size() == sg.stream.size();
            for (size_t i = 0; tail && i < ours.size(); i++) tail = ours[i] == sg.stream[i];
            // the header: only the bytes the load list reads (count, date, per manager 2 + 0x13 + 0x13)
            const Bytes h = save_header(g);
            int header_diffs = 0;
            for (size_t i = 0; i < h.size() && i < sg.plain_header.size(); i++) {
                const size_t k = i < 12 ? 0 : (i - 12) % 0x28;
                const size_t m = i < 12 ? 0 : (i - 12) / 0x28;
                if (i >= 12 && (m >= h[0] || k >= 0x28)) continue;
                if (i < 12 || k < 2 + 0x13 + 0x13) {
                    // names are compared up to their NUL
                    if (i >= 12 && k >= 2) {
                        const size_t start = 12 + m * 0x28 + (k < 0x15 ? 2 : 0x15);
                        bool after_nul = false;
                        for (size_t j = start; j < i; j++) after_nul = after_nul || sg.plain_header[j] == 0;
                        if (after_nul) continue;
                    }
                    if (h[i] != sg.plain_header[i]) header_diffs++;
                }
            }
            Savegame copy = sg;
            copy.stream = ours;
            const Bytes file = read_file(argv[a]);
            Bytes written = savegame_bytes(copy);
            // (the original header keeps stack bytes: take its header for the file comparison)
            std::printf("%s: stream %s, header %s, file %s\n", argv[a], tail ? "identical" : "DIFF",
                        header_diffs ? "DIFF" : "OK", written == file ? "identical" : "DIFF");
            if (!tail || header_diffs || written != file) bad++;
        } catch (const std::exception& e) {
            std::printf("%s: error %s\n", argv[a], e.what());
            bad++;
        }
    }
    return bad ? 2 : 0;
}
