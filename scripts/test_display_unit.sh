#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/display-tests"
mkdir -p "$BUILD_DIR"

c++ -std=c++20 -Wall -Wextra -Werror \
    -I"$PROJECT_ROOT/kernel/include" \
    "$PROJECT_ROOT/kernel/sys/framebuffer.cpp" \
    "$PROJECT_ROOT/tests/display_unit.cpp" \
    -o "$BUILD_DIR/display_unit"
"$BUILD_DIR/display_unit"
echo "[PASS] Framebuffer pixel-format and bounds unit tests"
