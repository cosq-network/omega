#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
QEMU_PIDS=()
cleanup_qemu() { for pid in "${QEMU_PIDS[@]}"; do kill -9 "${pid}" 2>/dev/null || true; done; }
trap cleanup_qemu EXIT
bash "${PROJECT_ROOT}/scripts/test_storage_unit.sh"

build_arch() {
    local arch="$1"; local toolchain="$2"
    cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}/${arch}" \
        -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/${toolchain}" -DARCH="${arch}" >/dev/null
    cmake --build "${BUILD_DIR}/${arch}" >/dev/null
}
run_and_assert() {
    local arch="$1"; local log="${BUILD_DIR}/${arch}_storage_test.log"
    case "${arch}" in
        x86_64)
            qemu-system-x86_64 -kernel "${BUILD_DIR}/x86_64/omega.elf" -serial stdio -display none -vga std >"${log}" 2>&1 &
            ;;
        aarch64)
            qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -kernel "${BUILD_DIR}/aarch64/omega.elf" >"${log}" 2>&1 &
            ;;
        riscv64)
            qemu-system-riscv64 -M virt -cpu rv64 -bios default -nographic -kernel "${BUILD_DIR}/riscv64/omega.elf" >"${log}" 2>&1 &
            ;;
        *) echo "unknown architecture: ${arch}" >&2; return 1 ;;
    esac
    local qemu_pid=$!
    QEMU_PIDS+=("${qemu_pid}")
    sleep 4
    kill -9 "${qemu_pid}" 2>/dev/null || true
    grep -Fq "[TEST][PASS] Storage core memory block path" "${log}"
    grep -Fq "[TEST][PASS] Storage write and flush policy" "${log}"
    grep -Fq "System online. Entering idle loop" "${log}"
    echo "[PASS] ${arch} storage core integration"
}
build_arch x86_64 x86_64-toolchain.cmake
build_arch aarch64 aarch64-toolchain.cmake
build_arch riscv64 riscv64-toolchain.cmake
run_and_assert x86_64
run_and_assert aarch64
run_and_assert riscv64
for arch in aarch64 riscv64; do
    cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}/${arch}-virtio-block-test" \
        -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/${arch}-toolchain.cmake" \
        -DARCH="${arch}" -DENABLE_EXPERIMENTAL_VIRTIO_BLOCK=ON >/dev/null
    cmake --build "${BUILD_DIR}/${arch}-virtio-block-test" >/dev/null
    echo "[PASS] ${arch} experimental VirtIO-Block build"
done
echo "[PASS] Storage unit and multi-architecture integration tests"
