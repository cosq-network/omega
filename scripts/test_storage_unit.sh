#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="${PROJECT_ROOT}/build/storage-tests"
mkdir -p "${TEST_DIR}"
clang++ -std=c++20 -Wall -Wextra -I"${PROJECT_ROOT}/kernel/include" \
    "${PROJECT_ROOT}/tests/storage_unit.cpp" \
    "${PROJECT_ROOT}/kernel/sys/storage.cpp" \
    "${PROJECT_ROOT}/kernel/sys/dma.cpp" \
    "${PROJECT_ROOT}/kernel/sys/memory_block.cpp" \
    "${PROJECT_ROOT}/kernel/sys/partition.cpp" \
    "${PROJECT_ROOT}/kernel/sys/ahci.cpp" \
    "${PROJECT_ROOT}/kernel/sys/nvme.cpp" \
    -o "${TEST_DIR}/storage_unit"
"${TEST_DIR}/storage_unit"
echo "[PASS] Storage host unit tests"
