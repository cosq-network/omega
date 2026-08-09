#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/boot-framebuffer-tests"
mkdir -p "$BUILD_DIR"

c++ -std=c++20 -Wall -Wextra -Werror \
    -I"$PROJECT_ROOT/kernel/include" -I"$PROJECT_ROOT/kernel/arch/x86_64" \
    "$PROJECT_ROOT/kernel/arch/x86_64/boot_fb.cpp" \
    "$PROJECT_ROOT/tests/boot_framebuffer_unit.cpp" \
    -o "$BUILD_DIR/boot_framebuffer_unit"
"$BUILD_DIR/boot_framebuffer_unit"
echo "[PASS] Multiboot framebuffer handoff parser unit tests"
