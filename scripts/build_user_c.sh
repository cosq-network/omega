#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${1:-x86_64}"
BUILD_DIR="${PROJECT_ROOT}/build/userland-c-${ARCH}"
mkdir -p "${BUILD_DIR}"

case "${ARCH}" in
    x86_64)
        TARGET=x86_64-unknown-none-elf
        CRT_ASM=user/omega_crt0_x86_64.s
        SYSCALL_ASM=user/omega_syscall_x86_64.s
        LINKER=user/linker.ld
        EXTRA="-mno-red-zone -mcmodel=large"
        ;;
    aarch64)
        TARGET=aarch64-unknown-none
        CRT_ASM=user/omega_crt0_aarch64.s
        SYSCALL_ASM=user/omega_syscall_aarch64.s
        LINKER=user/linker-aarch64.ld
        EXTRA=""
        ;;
    riscv64)
        TARGET=riscv64-unknown-none-elf
        CRT_ASM=user/omega_crt0_riscv64.s
        SYSCALL_ASM=user/omega_syscall_riscv64.s
        LINKER=user/linker-riscv64.ld
        EXTRA="-march=rv64gc -mabi=lp64d -mcmodel=medany"
        ;;
    *) echo "unsupported architecture: ${ARCH}" >&2; exit 2 ;;
esac

CLANG_BIN="${CLANG_BIN:-clang}"
if [[ "${ARCH}" == riscv64 && -x /opt/homebrew/opt/llvm/bin/clang ]]; then
    CLANG_BIN=/opt/homebrew/opt/llvm/bin/clang
fi
INCLUDE="-I${PROJECT_ROOT}/user/include"
COMMON="--target=${TARGET} -ffreestanding -fno-builtin -fno-stack-protector -nostdinc -Wno-unused-command-line-argument ${INCLUDE}"

${CLANG_BIN} ${COMMON} ${EXTRA} -c "${PROJECT_ROOT}/user/omega_crt.c" -o "${BUILD_DIR}/omega_crt.o"
${CLANG_BIN} ${COMMON} ${EXTRA} -c "${PROJECT_ROOT}/user/init_c.c" -o "${BUILD_DIR}/init_c.o"
${CLANG_BIN} --target="${TARGET}" ${EXTRA} -c "${PROJECT_ROOT}/${CRT_ASM}" -o "${BUILD_DIR}/crt0.o"
${CLANG_BIN} --target="${TARGET}" ${EXTRA} -c "${PROJECT_ROOT}/${SYSCALL_ASM}" -o "${BUILD_DIR}/syscall.o"
ld.lld -T "${PROJECT_ROOT}/${LINKER}" "${BUILD_DIR}/crt0.o" "${BUILD_DIR}/syscall.o" \
    "${BUILD_DIR}/omega_crt.o" "${BUILD_DIR}/init_c.o" -o "${BUILD_DIR}/init.elf"
echo "${BUILD_DIR}/init.elf"
