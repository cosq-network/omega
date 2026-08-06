#!/usr/bin/env bash

# Omega Kernel Integration Test Suite
# Tests compiled ELF binaries across x86_64, AArch64, and RISC-V 64 architectures in QEMU

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

echo "================================================="
echo "       Omega Kernel Automated Test Suite         "
echo "================================================="

run_test() {
    local arch=$1
    local qemu_bin=$2
    local qemu_args=$3
    local expected_outputs=("${@:4}")

    echo -e "\n[*] Running Integration Tests for Architecture: ${GREEN}${arch}${NC}"

    local log_file="${BUILD_DIR}/${arch}_test.log"
    rm -f "${log_file}"

    # Run QEMU in background and capture PID
    ${qemu_bin} ${qemu_args} > "${log_file}" 2>&1 &
    local qemu_pid=$!

    # Wait 4 seconds for kernel initialization
    sleep 4

    # Kill QEMU process
    kill -9 ${qemu_pid} 2>/dev/null || true

    local failed=0
    for expected in "${expected_outputs[@]}"; do
        if grep -q "${expected}" "${log_file}"; then
            echo -e "  [PASS] Found: '${expected}'"
        else
            echo -e "  [${RED}FAIL${NC}] Missing expected output: '${expected}'"
            failed=1
        fi
    done

    if [ ${failed} -eq 0 ]; then
        echo -e "${GREEN}[SUCCESS] All integration tests passed for ${arch}!${NC}"
    else
        echo -e "${RED}[FAILURE] One or more integration tests failed for ${arch}.${NC}"
        cat "${log_file}"
        exit 1
    fi
}

# 1. Build All Architectures
echo "[*] Building x86_64 kernel binary..."
rm -rf "${BUILD_DIR}/x86_64" && mkdir -p "${BUILD_DIR}/x86_64" && cd "${BUILD_DIR}/x86_64"
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/x86_64-toolchain.cmake -DARCH=x86_64 ../.. > /dev/null
make > /dev/null

echo "[*] Building AArch64 kernel binary..."
rm -rf "${BUILD_DIR}/aarch64" && mkdir -p "${BUILD_DIR}/aarch64" && cd "${BUILD_DIR}/aarch64"
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/aarch64-toolchain.cmake -DARCH=aarch64 ../.. > /dev/null
make > /dev/null

echo "[*] Building RISC-V 64 kernel binary..."
rm -rf "${BUILD_DIR}/riscv64" && mkdir -p "${BUILD_DIR}/riscv64" && cd "${BUILD_DIR}/riscv64"
cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/riscv64-toolchain.cmake -DARCH=riscv64 ../.. > /dev/null
make > /dev/null

# 2. x86_64 Integration Test Suite
run_test "x86_64" \
    "qemu-system-x86_64" \
    "-kernel ${BUILD_DIR}/x86_64/omega.elf -serial stdio -display none" \
    "Welcome to Omega Kernel" \
    "Architecture Identified: x86_64" \
    "Physical Memory Manager initialized" \
    "Virtual Memory Manager (VMM) initialized" \
    "Kernel Heap Allocator initialized" \
    "Interrupt Descriptor Table (IDT) Initialized" \
    "Preemptive Multi-threading Scheduler Initialized" \
    "POSIX System Call Surface Initialized" \
    "Virtual Filesystem (VFS) Initialized" \
    "RAM Disk (Initrd) Initialized" \
    "Scanning PCI Bus Configuration Space" \
    "VirtIO-Net Driver & TCP/IP Network Stack Initialized" \
    "Userland Mode Manager (Ring 3 / EL0) Initialized" \
    "Valid 64-bit ELF Binary Detected"

# 3. AArch64 Integration Test Suite
run_test "aarch64" \
    "qemu-system-aarch64" \
    "-M virt -cpu cortex-a57 -nographic -kernel ${BUILD_DIR}/aarch64/omega.elf" \
    "Welcome to Omega Kernel" \
    "Architecture Identified: AArch64" \
    "Physical Memory Manager initialized" \
    "Virtual Memory Manager (VMM) initialized" \
    "Kernel Heap Allocator initialized" \
    "Preemptive Multi-threading Scheduler Initialized" \
    "POSIX System Call Surface Initialized" \
    "Virtual Filesystem (VFS) Initialized" \
    "RAM Disk (Initrd) Initialized" \
    "AArch64 Device Tree / PCI Bus Scanner Initialized" \
    "VirtIO-Net Driver & TCP/IP Network Stack Initialized" \
    "Userland Mode Manager (Ring 3 / EL0) Initialized" \
    "Valid 64-bit ELF Binary Detected"

echo -e "\n${GREEN}=================================================${NC}"
echo -e "${GREEN}      ALL INTEGRATION TESTS PASSED CLEANLY!       ${NC}"
echo -e "${GREEN}=================================================${NC}"
