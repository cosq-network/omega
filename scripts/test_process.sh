#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/process-x86_64"
LOG_FILE="${BUILD_DIR}/process_test.log"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/x86_64-toolchain.cmake" \
    -DARCH=x86_64 >/dev/null
cmake --build "${BUILD_DIR}" >/dev/null

rm -f "${LOG_FILE}"
qemu-system-x86_64 -kernel "${BUILD_DIR}/omega.elf" -serial stdio -display none -vga std >"${LOG_FILE}" 2>&1 &
qemu_pid=$!
trap 'kill -9 "${qemu_pid}" 2>/dev/null || true' EXIT
sleep 3

grep -Fq "Linux-compatible process/address-space manager initialized (PID 1)" "${LOG_FILE}"
grep -Fq "[TEST][PASS] Isolated process address-space map/unmap" "${LOG_FILE}"
grep -Fq "[TEST][PASS] COW fork, write fault, exit, and wait/reap" "${LOG_FILE}"
grep -Fq "[TEST][PASS] Process address-space and lifecycle path" "${LOG_FILE}"
grep -Fq "System online. Entering idle loop" "${LOG_FILE}"
echo "[SUCCESS] Process address-space isolation integration test passed."
