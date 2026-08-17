#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="${PROJECT_ROOT}/build/elf-tests"
mkdir -p "${TEST_DIR}"
if [[ "$(uname -s)" == "Darwin" ]]; then
    LINK_GC=(-Wl,-dead_strip)
else
    LINK_GC=(-Wl,--gc-sections)
fi
# The unit exercises validate() only. Keep the production loader's mapping
# code in the same translation unit, but let the linker discard those
# unreferenced sections so this host test does not need the full PMM/VMM stack.
clang++ -std=c++20 -Wall -Wextra -ffunction-sections -fdata-sections \
    "${LINK_GC[@]}" -I"${PROJECT_ROOT}/kernel/include" \
    "${PROJECT_ROOT}/tests/elf_loader_unit.cpp" \
    "${PROJECT_ROOT}/kernel/sys/elf_loader.cpp" \
    -o "${TEST_DIR}/elf_loader_unit"
"${TEST_DIR}/elf_loader_unit"
echo "[PASS] Linux ELF executable/shared-object validation tests"
