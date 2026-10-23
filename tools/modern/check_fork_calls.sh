#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# check_fork_calls.sh BUILD_DIR - fails if a Modern library (LIBRARY, default bmh_season_modern)
# calls a Classic season
# function. Modern season code must call its own fork (namespace bmh::modern); a call that
# resolves to a function defined in one of the forked Classic sources means a Modern
# declaration is missing (see docs/new_engine). Windows/MSVC builds only (uses dumpbin).
#
#   tools/modern/check_fork_calls.sh build
set -euo pipefail
build=${1:-build}
config=${CONFIG:-Release}
library=${LIBRARY:-bmh_season_modern}
dumpbin=${DUMPBIN:-$(ls -d "/c/Program Files/Microsoft Visual Studio/"*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe 2>/dev/null | tail -1)}
[ -x "$dumpbin" ] || { echo "dumpbin not found (set DUMPBIN)" >&2; exit 2; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

symbols() { MSYS_NO_PATHCONV=1 "$dumpbin" /symbols "$1"; }
# Undefined external symbols of the Modern library.
symbols "$build/$config/$library.lib" |
    awk '/External/ && /UNDEF/ { i = index($0, "| "); s = substr($0, i + 2); sub(/ \(.*/, "", s); print s }' |
    sort -u > "$work/undef"
# Externals defined by the Classic objects that were forked (the Modern sources' names).
: > "$work/defs"
for source in src/modern/game/*.cpp; do
    name=$(basename "$source" .cpp)
    obj="$build/bmhcore.dir/$config/$name.obj"
    [ -f "$obj" ] || continue
    symbols "$obj" |
        awk -v f="$name" '/External/ && !/UNDEF/ { i = index($0, "| "); s = substr($0, i + 2); d = s; sub(/ \(.*/, "", s); print s "\t" f "\t" d }' >> "$work/defs"
done
sort -t$'\t' -k1,1 -u "$work/defs" > "$work/defs.sorted"
join -t$'\t' "$work/undef" "$work/defs.sorted" > "$work/hits" || true
if [ -s "$work/hits" ]; then
    echo "Modern season code calls Classic season functions:"
    cut -f2,3 "$work/hits" | sed 's/^\([a-z_]*\)\t/\1: /'
    exit 1
fi
echo "OK: $library calls no Classic season function."
