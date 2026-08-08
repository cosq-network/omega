#!/usr/bin/env bash
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="${PROJECT_ROOT}/build/security-tests"
mkdir -p "${TEST_DIR}"
clang++ -std=c++20 -Wall -Wextra -I"${PROJECT_ROOT}/kernel/include" \
    "${PROJECT_ROOT}/tests/security_unit.cpp" \
    "${PROJECT_ROOT}/kernel/sys/security.cpp" \
    "${PROJECT_ROOT}/kernel/sys/vfs.cpp" \
    -o "${TEST_DIR}/security_unit"
"${TEST_DIR}/security_unit"
echo "[PASS] Linux credential and VFS permission tests"
