# Standalone community data

The 1.1 community edition can start from `COMMUNITY.OBD` without an installed DOS game. The stable main line and loose Classic installation support remain separate.

`COMMUNITY.OBD` is a compressed read-only resource bundle. It contains the master database, text, palette, formations, desktop layout, fonts, indexed pictures/crests and audio containers required by OpenBMH. These are game content, not application code. Saves, highscores and user layouts remain normal writable files outside the bundle.

The bundle's `RUNTIME.DAT` uses the `OBMHDAT1` schema: data-segment base/length, calendar epoch, resolved badge/environment policies, data-segment bytes and explicitly named auxiliary labels. The exporter discards the DOS header, DOS/4GW loader and machine-code objects. Calendar and verified edition policies are values, rather than retained code fingerprints. This is not a renamed or embedded BMH executable.

The Classic address-view adapter still supplies the existing engine/UI table readers. It reads resource entries directly from the compressed bundle, without unpacking original executables or a cached DOS installation. Bundle paths are case-insensitive and reject duplicate/path-traversal entries; compressed/uncompressed limits bound allocations. Original loose installations retain their loading path.

Offline preparation from privately supplied resources:

```sh
cmake --build build --target export_community_runtime
python3 tools/poc/bundle_community.py --source /path/to/prepared-data \
  --exporter /path/to/build/export_community_runtime --out /path/to/new/community-data
```

Only required resource types are included. DOS programs, installers, editor executables, original saves, backup directories, manuals and unused picture formats are excluded. Import provenance stays in `IMPORT-REPORT.json`; `PACKAGE.json` records the hashes of the bundled resources. A bundled `OBMHPROF.INI` selects the expanded league profile, so no additional loose Original files are needed.

Generated foreign crests now use the supplied Original domestic crest background texture. Transparent edges are composited onto that bright texture before palette reduction; actual black logo details remain black. The previous black rectangles and the light-on-dark monochrome workaround are removed.
