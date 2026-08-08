#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/scheduler-x86_64"
LOG_FILE="${BUILD_DIR}/scheduler_test.log"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/x86_64-toolchain.cmake" \
    -DARCH=x86_64 -DENABLE_SCHEDULER_SELF_TEST=ON >/dev/null
cmake --build "${BUILD_DIR}" >/dev/null

rm -f "${LOG_FILE}"
qemu-system-x86_64 -kernel "${BUILD_DIR}/omega.elf" -serial stdio -display none -vga std >"${LOG_FILE}" 2>&1 &
qemu_pid=$!
trap 'kill -9 "${qemu_pid}" 2>/dev/null || true' EXIT
sleep 3

grep -Fq "x86_64 PIT timer initialized at 100 Hz" "${LOG_FILE}"
grep -Fq "[TEST][PASS] PIT timer preempted two kernel threads" "${LOG_FILE}"
grep -Fq "[TEST][PASS] Timer tick rate observed" "${LOG_FILE}"
grep -Fq "[TEST][PASS] Timer context-switch state preserved" "${LOG_FILE}"
grep -Fq "[TEST][PASS] Display console write path" "${LOG_FILE}"
echo "[SUCCESS] Timer-driven scheduler integration test passed."
