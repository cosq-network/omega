#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

run_c_app() {
    local arch="$1" kernel_build="$2" qemu="$3" qemu_args="$4" marker="$5" initrd_addr="$6"
    local build_dir="${PROJECT_ROOT}/build/sdk-c-${arch}"
    mkdir -p "${build_dir}"
    local init_elf
    init_elf="$(bash "${PROJECT_ROOT}/scripts/build_user_c.sh" "${arch}")"
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
    grep -Fq "${marker}" "${log_file}"
    grep -Fq "[!] Syscall Exit Called with status: 0" "${log_file}"
    echo "[PASS] ${arch} Omega C SDK application"
    trap - EXIT
}

run_c_app x86_64 x86_64 qemu-system-x86_64 \
    "-serial stdio -display none -vga std" \
    "Omega C SDK init is alive" 0x600000
run_c_app aarch64 aarch64 qemu-system-aarch64 \
    "-M virt -cpu cortex-a57 -nographic" \
    "Omega C SDK init is alive" 0x44000000
run_c_app riscv64 riscv64 qemu-system-riscv64 \
    "-M virt -cpu rv64 -bios default -nographic" \
    "Omega C SDK init is alive" 0x81000000

echo "[SUCCESS] Omega C SDK application passed on all reference ISAs."
