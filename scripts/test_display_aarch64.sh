#!/usr/bin/env bash

# AArch64/RISC-V display HAL smoke test. QEMU virt commonly has no firmware
# simple-framebuffer node, so this verifies the shared display path and its
# safe serial fallback. A real DT framebuffer changes the backend marker to
# "Display: SimpleFb" and exercises the same console code.

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
LOG_FILE="${BUILD_DIR}/aarch64_display_test.log"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}/aarch64" \
    -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/aarch64-toolchain.cmake" \
    -DARCH=aarch64 >/dev/null
cmake --build "${BUILD_DIR}/aarch64" >/dev/null

qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic \
    -kernel "${BUILD_DIR}/aarch64/omega.elf" >"${LOG_FILE}" 2>&1 &
QEMU_PID=$!
sleep 2
kill "${QEMU_PID}" 2>/dev/null || true

grep -Fq "Welcome to Omega Kernel" "${LOG_FILE}"
grep -Eq "Display: (SimpleFb|No framebuffer backend found)" "${LOG_FILE}"
grep -Fq "[TEST][PASS] Display console write path" "${LOG_FILE}"
grep -Fq "System online. Entering idle loop" "${LOG_FILE}"

echo "[PASS] AArch64 display HAL and serial fallback integration"
