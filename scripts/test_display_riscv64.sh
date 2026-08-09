#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build/riscv64"
LOG_FILE="$BUILD_DIR/riscv64_display_test.log"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/riscv64-toolchain.cmake" \
    -DARCH=riscv64 >/dev/null
cmake --build "$BUILD_DIR" >/dev/null

qemu-system-riscv64 -M virt -cpu rv64 -bios default -nographic \
    -kernel "$BUILD_DIR/omega.elf" >"$LOG_FILE" 2>&1 &
QEMU_PID=$!
sleep 2
kill "$QEMU_PID" 2>/dev/null || true

grep -Fq "Welcome to Omega Kernel" "$LOG_FILE"
grep -Fq "Display: No framebuffer backend found" "$LOG_FILE"
grep -Fq "[TEST][PASS] Display console write path" "$LOG_FILE"
grep -Fq "System online. Entering idle loop" "$LOG_FILE"

echo "[PASS] RISC-V display HAL and serial fallback integration"
