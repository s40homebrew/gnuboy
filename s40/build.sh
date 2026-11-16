#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail
PORT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
export S40_SDK_ROOT=${S40_SDK_ROOT:-$(CDPATH='' cd -- "$PORT_DIR/../.." && pwd)/main}
export S40_APP_DIR="$PORT_DIR"
export S40_BUILD_DIR="$PORT_DIR/build"
export S40_DIST_DIR="$PORT_DIR/dist"
exec bash "$S40_SDK_ROOT/homebrew/build.sh" gnuboy
