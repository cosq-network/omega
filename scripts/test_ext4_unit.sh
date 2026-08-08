#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="${PROJECT_ROOT}/build/storage-tests"
mkdir -p "${TEST_DIR}"
clang++ -std=c++20 -Wall -Wextra -I"${PROJECT_ROOT}/kernel/include" \
    "${PROJECT_ROOT}/tests/ext4_unit.cpp" \
    "${PROJECT_ROOT}/kernel/sys/ext4.cpp" \
    "${PROJECT_ROOT}/kernel/sys/vfs.cpp" \
    "${PROJECT_ROOT}/kernel/sys/security.cpp" \
    "${PROJECT_ROOT}/kernel/sys/storage.cpp" \
    "${PROJECT_ROOT}/kernel/sys/dma.cpp" \
    "${PROJECT_ROOT}/kernel/sys/memory_block.cpp" \
    -o "${TEST_DIR}/ext4_unit"
"${TEST_DIR}/ext4_unit"
echo "[PASS] ext4 filesystem unit tests"
