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
run_x86_virtio_completion() {
    local build_dir="${BUILD_DIR}/x86_64-virtio-block-test"
    local image="${build_dir}/profile-runtime.raw"
    local log="${BUILD_DIR}/x86_64_virtio_block_runtime.log"
    cmake -S "${PROJECT_ROOT}" -B "${build_dir}" \
        -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/x86_64-toolchain.cmake" \
        -DARCH=x86_64 >/dev/null
    cmake --build "${build_dir}" >/dev/null
    mkdir -p "${build_dir}"
    dd if=/dev/zero of="${image}" bs=1M count=16 status=none
    qemu-system-x86_64 -machine pc -no-reboot -display none -vga std -serial stdio \
        -kernel "${build_dir}/omega.elf" \
        -drive "file=${image},format=raw,if=none,id=storage0" \
        -device virtio-blk-pci,disable-modern=on,drive=storage0 >"${log}" 2>&1 &
    local qemu_pid=$!
    QEMU_PIDS+=("${qemu_pid}")
    sleep 4
    kill -9 "${qemu_pid}" 2>/dev/null || true
    for marker in \
        "[+] VirtIO-Block PCI runtime completion enabled." \
        "[TEST][PASS] VirtIO-Block read completion" \
        "[TEST][PASS] VirtIO-Block write/read completion" \
        "[TEST][PASS] VirtIO-Block flush completion"; do
        grep -Fq "${marker}" "${log}" || { cat "${log}"; echo "[FAIL] x86_64 VirtIO-Block runtime: ${marker}" >&2; return 1; }
    done
    echo "[PASS] x86_64 VirtIO-Block runtime completion"
}
run_x86_nvme_completion() {
    local build_dir="${BUILD_DIR}/x86_64-nvme-test"
    local image="${build_dir}/profile-runtime.raw"
    local log="${BUILD_DIR}/x86_64_nvme_runtime.log"
    cmake -S "${PROJECT_ROOT}" -B "${build_dir}" \
        -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/x86_64-toolchain.cmake" \
        -DARCH=x86_64 >/dev/null
    cmake --build "${build_dir}" >/dev/null
    mkdir -p "${build_dir}"
    dd if=/dev/zero of="${image}" bs=1M count=16 status=none
    qemu-system-x86_64 -machine pc -no-reboot -display none -vga std -serial stdio \
        -kernel "${build_dir}/omega.elf" \
        -drive "file=${image},format=raw,if=none,id=nvme0" \
        -device nvme,drive=nvme0,serial=1234 >"${log}" 2>&1 &
    local qemu_pid=$!
    QEMU_PIDS+=("${qemu_pid}")
    sleep 4
    kill -9 "${qemu_pid}" 2>/dev/null || true
    for marker in \
        "[NVME] Controller started and ready." \
        "[NVME] IDENTIFY Controller complete." \
        "[TEST][PASS] NVMe write/read completion"; do
        grep -Fq "${marker}" "${log}" || { cat "${log}"; echo "[FAIL] x86_64 NVMe runtime: ${marker}" >&2; return 1; }
    done
    echo "[PASS] x86_64 NVMe runtime completion"
}
run_riscv_mmio_completion() {
    local build_dir="${BUILD_DIR}/riscv64-virtio-block-test"
    local image="${build_dir}/profile-runtime.raw"
    local log="${BUILD_DIR}/riscv64_virtio_mmio_runtime.log"
    cmake -S "${PROJECT_ROOT}" -B "${build_dir}" \
        -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/riscv64-toolchain.cmake" \
        -DARCH=riscv64 >/dev/null
    cmake --build "${build_dir}" >/dev/null
    mkdir -p "${build_dir}"
    dd if=/dev/zero of="${image}" bs=1M count=16 status=none
    qemu-system-riscv64 -M virt -cpu rv64 -bios default -nographic \
        -kernel "${build_dir}/omega.elf" \
        -drive "file=${image},format=raw,if=none,id=storage0" \
        -device virtio-blk-device,drive=storage0 >"${log}" 2>&1 &
    local qemu_pid=$!
    QEMU_PIDS+=("${qemu_pid}")
    sleep 5
    kill -9 "${qemu_pid}" 2>/dev/null || true
    for marker in \
        "[+] VirtIO-Block storage device initialized." \
        "[TEST][PASS] VirtIO-Block read completion" \
        "[TEST][PASS] VirtIO-Block write/read completion"; do
        grep -Fq "${marker}" "${log}" || { cat "${log}"; echo "[FAIL] RISC-V VirtIO-MMIO runtime: ${marker}" >&2; return 1; }
    done
    grep -Fq "[TEST][PASS] VirtIO-Block flush completion" "${log}" || \
        grep -Fq "[TEST][SKIP] VirtIO-Block flush feature unavailable" "${log}"
    echo "[PASS] RISC-V VirtIO-MMIO runtime completion"
}
run_aarch64_mmio_completion() {
    local build_dir="${BUILD_DIR}/aarch64-virtio-block-test"
    local image="${build_dir}/profile-runtime.raw"
    local log="${BUILD_DIR}/aarch64_virtio_mmio_runtime.log"
    cmake -S "${PROJECT_ROOT}" -B "${build_dir}" \
        -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/aarch64-toolchain.cmake" \
        -DARCH=aarch64 >/dev/null
    cmake --build "${build_dir}" >/dev/null
    mkdir -p "${build_dir}"
    dd if=/dev/zero of="${image}" bs=1M count=16 status=none
    qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic \
        -kernel "${build_dir}/omega.elf" \
        -drive "file=${image},format=raw,if=none,id=storage0" \
        -device virtio-blk-device,drive=storage0 >"${log}" 2>&1 &
    local qemu_pid=$!
    QEMU_PIDS+=("${qemu_pid}")
    sleep 5
    kill -9 "${qemu_pid}" 2>/dev/null || true
    for marker in \
        "[+] VirtIO-Block storage device initialized." \
        "[TEST][PASS] VirtIO-Block read completion" \
        "[TEST][PASS] VirtIO-Block write/read completion"; do
        grep -Fq "${marker}" "${log}" || { cat "${log}"; echo "[FAIL] AArch64 VirtIO-MMIO runtime: ${marker}" >&2; return 1; }
    done
    grep -Fq "[TEST][PASS] VirtIO-Block flush completion" "${log}" || \
        grep -Fq "[TEST][SKIP] VirtIO-Block flush feature unavailable" "${log}"
    echo "[PASS] AArch64 VirtIO-MMIO runtime completion"
}
build_arch x86_64 x86_64-toolchain.cmake
build_arch aarch64 aarch64-toolchain.cmake
build_arch riscv64 riscv64-toolchain.cmake
run_and_assert x86_64
run_and_assert aarch64
run_and_assert riscv64
run_x86_virtio_completion
#run_x86_nvme_completion
run_riscv_mmio_completion
run_aarch64_mmio_completion
echo "[PASS] Storage unit and multi-architecture integration tests"
