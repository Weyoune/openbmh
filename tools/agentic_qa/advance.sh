#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# advance.sh <seat> <turns> - plays up to <turns> of this seat's turns in the session of
# $QA_DIR: WaitTurn, Advice, then GoBack + Kickoff. Stops early and says why when the
# manager has to decide himself:
#   == question     an open question (answer with send.sh <seat> 'Answer(<id>, <option>)')
#   == match        a live match runs: look (ShowMatchStats, ShowTactics, ShowSquad), intervene
#                   (SetTactic, Substitute, MovePlayer), then send Run(Halftime) or Run
#                   (the clock moves when every manager has sent his)
#   == season end   a final table came: inspect the new season before continuing
#   == game over
# Prints a condensed view: advice, messages, questions, results, chat, errors.
: "${QA_DIR:?set QA_DIR to the session folder}"
CLI=${CLI:-openbmh_cli}
SEAT="$QA_DIR/box/seat$1"; N="$2"
send() { printf '%s\ngo.\n' "$1" | "$CLI" --send "$SEAT" --timeout 3600; }
show() { grep -E "^(i advice|i New message|i final table|\? Q|! |c \[|# Session|\+ Next day|\+ Q|i [0-9]+')" | grep -v 'nothing to go back' | cut -c1-300; }
stop_if() {
    echo "$1" | grep -q '# Session ended' && { echo "== game over"; exit 0; }
    echo "$1" | grep -q '^? Q' && { echo "== question: answer it"; exit 0; }
    echo "$1" | grep -q 'Window: Match' && { echo "== match: intervene if you want, then Run(Halftime) or Run"; exit 0; }
    echo "$1" | grep -q 'final table' && { echo "== season end: inspect before continuing"; exit 0; }
}
for ((t = 1; t <= N; t++)); do
    r=$(send "WaitTurn"); echo "$r" | show; stop_if "$r"
    r=$(send 'Advice'); echo "$r" | show
    r=$(send $'GoBack\nGoBack\nKickoff'); echo "$r" | show; stop_if "$r"
    echo "-- turn $t done"
done
