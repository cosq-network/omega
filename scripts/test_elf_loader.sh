#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="${PROJECT_ROOT}/build/elf-tests"
mkdir -p "${TEST_DIR}"
clang++ -std=c++20 -Wall -Wextra -I"${PROJECT_ROOT}/kernel/include" \
    "${PROJECT_ROOT}/tests/elf_loader_unit.cpp" \
    "${PROJECT_ROOT}/kernel/sys/elf_loader.cpp" \
    -o "${TEST_DIR}/elf_loader_unit"
"${TEST_DIR}/elf_loader_unit"
echo "[PASS] Linux ELF executable/shared-object validation tests"
