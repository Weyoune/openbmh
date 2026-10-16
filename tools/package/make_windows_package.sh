#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Compatibility entry point; CMake installs the app-local compiler runtime.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
python3 "$ROOT/tools/package/make_package.py" --build "${1:-$ROOT/build}" --out "${2:-$ROOT/dist}" --platform windows-x64
