#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/userland-x86_64"
mkdir -p "${BUILD_DIR}"

INIT_ELF="$(bash "${PROJECT_ROOT}/scripts/build_user_init.sh")"
python3 "${PROJECT_ROOT}/scripts/create_initrd.py" "${BUILD_DIR}/initrd.img" "${INIT_ELF}"
cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}/kernel" \
    -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/x86_64-toolchain.cmake" -DARCH=x86_64 >/dev/null
cmake --build "${BUILD_DIR}/kernel" >/dev/null

LOG_FILE="${BUILD_DIR}/qemu.log"
qemu-system-x86_64 -kernel "${BUILD_DIR}/kernel/omega.elf" \
    -device loader,file="${BUILD_DIR}/initrd.img",addr=0x600000 \
    -serial stdio -display none -vga std \
    >"${LOG_FILE}" 2>&1 &
QEMU_PID=$!
trap 'kill -9 "${QEMU_PID}" 2>/dev/null || true' EXIT
sleep 3
kill -9 "${QEMU_PID}" 2>/dev/null || true

grep -Fq "[TEST][PASS] PID 1 userspace address space activated" "${LOG_FILE}"
grep -Fq "Omega userspace init: Ring 3 syscall path is alive" "${LOG_FILE}"
echo "[SUCCESS] x86_64 userspace init, Ring 3 entry, and syscall path passed."
