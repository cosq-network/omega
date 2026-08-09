#!/usr/bin/env bash

# Omega VGA / System Display Module — boot-time self-test and QEMU integration tests.
# Exercises Bochs VBE (1024x768x32), DISPI register readback, framebuffer draw path,
# and VGA text mode fallback (-vga none).

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/x86_64"
LOG_FILE="${BUILD_DIR}/display_test.log"
QEMU_PIDS=()
cleanup_qemu() { for pid in "${QEMU_PIDS[@]}"; do kill -9 "${pid}" 2>/dev/null || true; done; }
trap cleanup_qemu EXIT

echo "================================================="
echo "   Omega VGA Display Module Test Suite           "
echo "================================================="

echo "[*] Building x86_64 kernel..."
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/x86_64-toolchain.cmake -DARCH=x86_64 ../.. > /dev/null
make > /dev/null

assert_log_contains() {
    local needle="$1"
    if grep -Fq "${needle}" "${LOG_FILE}"; then
        echo -e "  [PASS] Found: '${needle}'"
    else
        echo -e "  [${RED}FAIL${NC}] Missing: '${needle}'"
        cat "${LOG_FILE}"
        exit 1
    fi
}

run_qemu_capture() {
    local extra_args="$1"
    rm -f "${LOG_FILE}"
    # shellcheck disable=SC2086
    qemu-system-x86_64 \
        -kernel "${BUILD_DIR}/omega.elf" \
        -serial stdio \
        -display none \
        ${extra_args} > "${LOG_FILE}" 2>&1 &
    local pid=$!
    QEMU_PIDS+=("${pid}")
    sleep 4
    kill -9 "${pid}" 2>/dev/null || true
}

echo -e "\n[*] Integration test: Standard VGA / Bochs VBE (-vga std)"
run_qemu_capture "-vga std"
assert_log_contains "[+] Display: BochsVbe"
assert_log_contains "[TEST][SKIP] VGA text buffer (linear framebuffer active)"
assert_log_contains "[TEST][PASS] Bochs VBE DISPI register readback"
assert_log_contains "[TEST][PASS] Bochs VBE linear framebuffer pixel"
assert_log_contains "[TEST][PASS] Framebuffer draw path"
assert_log_contains "[TEST][PASS] Display console write path"
assert_log_contains "Welcome to Omega Kernel"
assert_log_contains "Physical Memory Manager initialized"

echo -e "\n[*] Integration test: explicit Bochs VGA PCI device (-device VGA)"
run_qemu_capture "-device VGA"
assert_log_contains "[+] Display: BochsVbe"
assert_log_contains "[TEST][PASS] Bochs VBE DISPI register readback"
assert_log_contains "[TEST][PASS] Bochs VBE linear framebuffer pixel"

echo -e "\n[*] Integration test: VGA text fallback (-vga cirrus)"
run_qemu_capture "-vga cirrus"
assert_log_contains "[+] Display: VgaText 80x25"
assert_log_contains "[TEST][PASS] VGA text buffer read/write"
assert_log_contains "[TEST][SKIP] Bochs VBE linear framebuffer pixel"
assert_log_contains "[TEST][PASS] Display console write path"
assert_log_contains "Welcome to Omega Kernel"

echo -e "\n[*] Integration test: no VGA backend fallback (-vga none)"
run_qemu_capture "-vga none"
assert_log_contains "[!] Display: No usable output backend found"
assert_log_contains "[TEST][SKIP] VGA text buffer (no VGA backend)"
assert_log_contains "[TEST][PASS] Display console write path"
assert_log_contains "Welcome to Omega Kernel"

echo -e "\n[*] Regression test: kernel still reaches ELF loader on serial"
run_qemu_capture "-vga std"
assert_log_contains "Valid 64-bit ELF Binary Detected"

echo -e "\n${GREEN}=================================================${NC}"
echo -e "${GREEN}   ALL VGA DISPLAY TESTS PASSED                 ${NC}"
echo -e "${GREEN}=================================================${NC}"
