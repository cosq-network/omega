#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/input-tests"
mkdir -p "$BUILD_DIR"

c++ -std=c++20 -Wall -Wextra -Werror \
    -I"$PROJECT_ROOT/kernel/include" \
    "$PROJECT_ROOT/kernel/sys/input.cpp" \
    "$PROJECT_ROOT/tests/input_unit.cpp" \
    -o "$BUILD_DIR/input_unit"
"$BUILD_DIR/input_unit"
echo "[PASS] Input ABI, queue, HID boot decoder, and PS/2 decoder unit tests"
