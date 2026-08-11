#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${1:-x86_64}"
BUILD_DIR="${PROJECT_ROOT}/build/userland-${ARCH}"
mkdir -p "${BUILD_DIR}"
case "${ARCH}" in
    x86_64) TARGET=x86_64-unknown-none-elf; SOURCE=user/init.s; LINKER=user/linker.ld; EXTRA= ;;
    aarch64) TARGET=aarch64-unknown-none; SOURCE=user/init_aarch64.s; LINKER=user/linker-aarch64.ld; EXTRA= ;;
    riscv64) TARGET=riscv64-unknown-none; SOURCE=user/init_riscv64.s; LINKER=user/linker-riscv64.ld; EXTRA="-march=rv64gc -mabi=lp64d" ;;
    *) echo "unsupported architecture: ${ARCH}" >&2; exit 2 ;;
esac
CLANG_BIN="${CLANG_BIN:-clang}"
if [[ "${ARCH}" == riscv64 && -x /opt/homebrew/opt/llvm/bin/clang ]]; then CLANG_BIN=/opt/homebrew/opt/llvm/bin/clang; fi
${CLANG_BIN} --target="${TARGET}" -ffreestanding -nostdlib \
    ${EXTRA} -c \
    "${PROJECT_ROOT}/${SOURCE}" -o "${BUILD_DIR}/init.o"
ld.lld -T "${PROJECT_ROOT}/${LINKER}" "${BUILD_DIR}/init.o" \
    -o "${BUILD_DIR}/init.elf"
echo "${BUILD_DIR}/init.elf"
