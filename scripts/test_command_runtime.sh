#!/usr/bin/env bash
# Execute /bin/echo through the hosted execve path on all reference ISAs.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE_SOURCE="${PROJECT_ROOT}/tests/command_exec_probe.c"

build_probe() {
    local arch="$1"
    local sdk="${PROJECT_ROOT}/libc/omega-sdk/${arch}"
    local out="${PROJECT_ROOT}/build/command-runtime-${arch}"
    mkdir -p "${out}"
    local target extra=()
    case "${arch}" in
        x86_64) target=x86_64-unknown-none-elf; extra=(-mno-red-zone -mcmodel=large) ;;
        aarch64) target=aarch64-unknown-none-elf ;;
        riscv64) target=riscv64-unknown-none-elf; extra=(-march=rv64gc -mabi=lp64d -mcmodel=medany) ;;
        *) return 2 ;;
    esac
    local clang_bin="${CLANG_BIN:-clang}"
    if [[ "${arch}" == riscv64 && "${clang_bin}" == clang && -x /opt/homebrew/opt/llvm/bin/clang ]]; then
        clang_bin=/opt/homebrew/opt/llvm/bin/clang
    fi
    local compile_flags=(--target="${target}" -std=c11 -D_GNU_SOURCE -O2 -ffreestanding
        -fno-stack-protector -fno-builtin -nostdinc -isystem "${sdk}/include")
    if ((${#extra[@]})); then compile_flags+=("${extra[@]}"); fi
    "${clang_bin}" "${compile_flags[@]}" -c "${PROBE_SOURCE}" -o "${out}/probe.o"
    local runtime=""
    [[ -f "${sdk}/lib/libcompiler_rt.a" ]] && runtime="${sdk}/lib/libcompiler_rt.a"
    [[ -z "${runtime}" && -f "${sdk}/lib/libtcc1.a" ]] && runtime="${sdk}/lib/libtcc1.a"
    ld.lld -flavor gnu -T "${sdk}/lib/omega.ld" -o "${out}/probe.elf" \
        "${sdk}/lib/crt1.o" "${out}/probe.o" "${sdk}/lib/libc.a" \
        "${sdk}/lib/libomega-shim.a" ${runtime}
    echo "${out}/probe.elf"
}

run_probe() {
    local arch="$1" kernel_build="$2" qemu="$3" qemu_args="$4" address="$5"
    local build_dir="${PROJECT_ROOT}/build/command-runtime-${arch}"
    local probe
    probe="$(build_probe "${arch}")"
    python3 "${PROJECT_ROOT}/scripts/create_initrd.py" "${build_dir}/initrd.img" "${probe}" \
        --file "bin/echo=${PROJECT_ROOT}/userland/commands/${arch}/bin/echo" >/dev/null
    cmake --build "${PROJECT_ROOT}/build/${kernel_build}" >/dev/null
    local log="${build_dir}/qemu.log"
    rm -f "${log}"
    # shellcheck disable=SC2086
    ${qemu} ${qemu_args} -kernel "${PROJECT_ROOT}/build/${kernel_build}/omega.elf" \
        -device "loader,file=${build_dir}/initrd.img,addr=${address}" >"${log}" 2>&1 &
    local pid=$!
    trap 'kill -9 "${pid}" 2>/dev/null || true' EXIT
    sleep 4
    kill -9 "${pid}" 2>/dev/null || true
    grep -Fq "Syscall Exit Called with status: 0" "${log}"
    ! grep -Fq "Invalid Syscall Number" "${log}"
    echo "[PASS] ${arch} execve /bin/echo runtime path"
    trap - EXIT
}

run_probe x86_64 x86_64 qemu-system-x86_64 "-serial stdio -display none" 0x600000
run_probe aarch64 aarch64 qemu-system-aarch64 "-M virt -cpu cortex-a57 -nographic" 0x44000000
run_probe riscv64 riscv64 qemu-system-riscv64 "-M virt -cpu rv64 -bios default -nographic" 0x81000000
echo "[SUCCESS] Hosted command execve probe completed; see any ISA limitations above."
