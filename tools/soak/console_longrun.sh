#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# console_longrun.sh - plays many careers on autopilot through the console and reports
# crashes, engine gaps and hangs. Every career: NewGame with the given options, the
# managers pick Bundesliga clubs, then each turn is "GoBack" (a question gets its default
# answer - sales declined, contracts not renewed, so squads thin out over the years) and
# "Kickoff", in --time instant, for as many turns as given. That reaches the rare paths
# agents and the regression do not: relegations down the leagues, nearly empty squads,
# money trouble, many season changes.
#
# Usage: tools/soak/console_longrun.sh <BMH data dir> <out dir> [turns=1200] [jobs=4] [runs...]
#   a run is "seed:managers:world:club1[,club2..]" (clubs are Bundesliga club numbers 0-17);
#   without runs a default mix of 12 careers is played.
# Output: <out dir>/<run>.txt (the full console output) and a summary on stdout:
#   one line per run with the last date, E_INTERNAL / E_ENGINE_GAP counts and the first of
#   each; "HANG" when the run did not finish within its time limit.
# Exit code 0 when no run has an E_INTERNAL or a hang.
set -u
DATA=${1:?BMH data dir}; OUT=${2:?out dir}; TURNS=${3:-1200}; JOBS=${4:-4}; shift $(( $# < 4 ? $# : 4 ))
EXE=${CLI:-openbmh_cli}
RUNS=("$@")
[ ${#RUNS[@]} -eq 0 ] && RUNS=(1:1:0:0 2:1:0:7 3:1:1:2 4:1:0:15 5:2:0:5,11 6:2:1:3,9 7:3:0:1,6,12 8:3:1:4,8,16
                              9:4:0:7,5,2,10 10:4:1:0,13,14,17 11:1:1:11 12:2:0:16,17)
mkdir -p "$OUT"

one() {  # one career: seed managers world clubs
    local seed=$1 managers=$2 world=$3 clubs=$4 name="s$1_m$2_w$3_c${4//,/-}"
    local dir="$OUT/$name.saves" in="$OUT/$name.in" out="$OUT/$name.txt"
    mkdir -p "$dir"
    {
        printf 'NewGame(5, 3, %d, 3, true, %s)\ngo.\n' "$managers" "$([ "$world" = 1 ] && echo true || echo false)"
        local k=1
        for c in ${clubs//,/ }; do
            printf 'SelectTeam(M%d, club:%d, Bundesliga, Middle, %d)\nAnswer(%d, Yes)\ngo.\n' $k "$c" $k $k
            k=$((k + 1))
        done
        for ((t = 0; t < TURNS; t++)); do printf 'GoBack\ngo.\nKickoff\ngo.\n'; done
        printf 'Quit(force=true)\ngo.\n'
    } > "$in"
    # a generous limit: about one second per turn
    timeout $((TURNS + 300)) "$EXE" --console --time instant --seed "$seed" --data "$DATA" \
        --saves "$(cygpath -w "$dir" 2>/dev/null || echo "$dir")" < "$in" > "$out" 2>&1
    local rc=$? last internal gap
    last=$(grep -o 'Next day: [0-9-]*\|Season end: [0-9-]*\|Game over[^"]*' "$out" | tail -1)
    internal=$(grep -c '^! E_INTERNAL' "$out")
    gap=$(grep -c '^! E_ENGINE_GAP' "$out")
    printf '%-24s %-26s internal %4d gap %4d%s\n' "$name" "${last:-no turn}" "$internal" "$gap" "$([ $rc = 124 ] && echo '  HANG')"
    grep -m1 '^! E_INTERNAL' "$out" | cut -c1-200 | sed 's/^/    first internal: /'
    grep -m1 '^! E_ENGINE_GAP' "$out" | cut -c1-200 | sed 's/^/    first gap: /'
}

running=0
for r in "${RUNS[@]}"; do
    IFS=: read -r seed managers world clubs <<< "$r"
    one "$seed" "$managers" "$world" "$clubs" > "$OUT/.summary-$seed-$managers-$world" &
    running=$((running + 1))
    if [ $running -ge "$JOBS" ]; then wait -n; running=$((running - 1)); fi
done
wait
cat "$OUT"/.summary-* | tee "$OUT/summary.txt"
! grep -q 'internal *[1-9]\|HANG' "$OUT/summary.txt"
