#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# send.sh <seat> '<command lines, separated by \n>' - one block to the session of
# $QA_DIR (see docs/CONSOLE_CLIENTS.md), prints the full answer.
# QA_DIR: the session folder with box/seat<k>; CLI: path to openbmh_cli.
: "${QA_DIR:?set QA_DIR to the session folder}"
CLI=${CLI:-openbmh_cli}
printf '%b\ngo.\n' "$2" | "$CLI" --send "$QA_DIR/box/seat$1" --timeout 3600
