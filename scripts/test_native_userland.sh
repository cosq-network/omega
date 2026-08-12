#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

run_native() {
    local arch="$1" kernel_build="$2" qemu="$3" qemu_args="$4" marker="$5" initrd_addr="$6"
    local build_dir="${PROJECT_ROOT}/build/userland-${arch}"
    mkdir -p "${build_dir}"
    local init_elf
    init_elf="$(bash "${PROJECT_ROOT}/scripts/build_user_init.sh" "${arch}")"
    python3 "${PROJECT_ROOT}/scripts/create_initrd.py" "${build_dir}/initrd.img" "${init_elf}" >/dev/null
    cmake --build "${PROJECT_ROOT}/build/${kernel_build}" >/dev/null
    local log_file="${build_dir}/qemu.log"
    rm -f "${log_file}"
    # shellcheck disable=SC2086
    ${qemu} ${qemu_args} -kernel "${PROJECT_ROOT}/build/${kernel_build}/omega.elf" \
        -device "loader,file=${build_dir}/initrd.img,addr=${initrd_addr}" >"${log_file}" 2>&1 &
    local qemu_pid=$!
    trap 'kill -9 "${qemu_pid}" 2>/dev/null || true' EXIT
    sleep 4
    kill -9 "${qemu_pid}" 2>/dev/null || true
    grep -Fq "[TEST][PASS] PID 1 userspace address space activated" "${log_file}"
    grep -Fq "${marker}" "${log_file}"
    echo "[PASS] ${arch} native userspace syscall path"
    trap - EXIT
}

run_native aarch64 aarch64 qemu-system-aarch64 \
    "-M virt -cpu cortex-a57 -nographic" \
    "Omega userspace init: AArch64 EL0 syscall path is alive" 0x44000000
run_native riscv64 riscv64 qemu-system-riscv64 \
    "-M virt -cpu rv64 -bios default -nographic" \
    "Omega userspace init: RISC-V U-mode syscall path is alive" 0x81000000

echo "[SUCCESS] Native AArch64 and RISC-V userspace integration passed."
