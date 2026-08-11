#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/userland-x86_64"
mkdir -p "${BUILD_DIR}"
clang --target=x86_64-unknown-none-elf -ffreestanding -nostdlib -c \
    "${PROJECT_ROOT}/user/init.s" -o "${BUILD_DIR}/init.o"
ld.lld -T "${PROJECT_ROOT}/user/linker.ld" "${BUILD_DIR}/init.o" \
    -o "${BUILD_DIR}/init.elf"
echo "${BUILD_DIR}/init.elf"
