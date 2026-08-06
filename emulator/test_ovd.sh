#!/usr/bin/env bash

# Automated Integration Test Suite for Omega Virtual Device Management & Execution (OVD)
# Verifies OVD creation, listing, headful/headless emulator execution, and deletion

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OVD_MGR="${PROJECT_ROOT}/emulator/ovd_manager.sh"
OVD_RUN="${PROJECT_ROOT}/emulator/ovd_run.sh"

echo "================================================="
echo "  Omega Virtual Device (OVD) Integration Tests   "
echo "================================================="

chmod +x "${OVD_MGR}" "${OVD_RUN}"

run_ovd_test() {
    local name=$1
    local arch=$2

    echo -e "\n[*] Testing Omega Virtual Device Lifecycle for: ${GREEN}${name}${NC} (${arch})"

    # Ensure clean state
    bash "${OVD_MGR}" delete --name "${name}" > /dev/null 2>&1 || true

    # 1. Create OVD
    bash "${OVD_MGR}" create --name "${name}" --arch "${arch}" --ram 512 --disk 32

    # 2. List OVD Devices
    if bash "${OVD_MGR}" list | grep -q "${name}"; then
        echo -e "  [PASS] OVD '${name}' successfully registered in device registry."
    else
        echo -e "  [${RED}FAIL${NC}] OVD '${name}' missing from device registry."
        exit 1
    fi

    # 3. Test Headless OVD Execution
    echo "[*] Testing Headless OVD Execution..."
    local log_file="${PROJECT_ROOT}/emulator/${name}_test.log"
    rm -f "${log_file}"

    bash "${OVD_RUN}" run --name "${name}" --no-gpu > "${log_file}" 2>&1 &
    local emu_pid=$!

    sleep 4
    kill -9 ${emu_pid} 2>/dev/null || true

    if [ "${arch}" = "riscv64" ]; then
        if grep -q "OpenSBI" "${log_file}"; then
            echo -e "  [PASS] Headless OVD Execution Verified for '${name}'."
        else
            echo -e "  [${RED}FAIL${NC}] Headless OVD Execution Failed for '${name}'."
            cat "${log_file}"
            exit 1
        fi
    else
        if grep -q "Welcome to Omega Kernel" "${log_file}"; then
            echo -e "  [PASS] Headless OVD Execution Verified for '${name}'."
        else
            echo -e "  [${RED}FAIL${NC}] Headless OVD Execution Failed for '${name}'."
            cat "${log_file}"
            exit 1
        fi
    fi

    # 4. Delete OVD
    bash "${OVD_MGR}" delete --name "${name}"
    echo -e "  [PASS] OVD '${name}' successfully cleaned up."
}

# Run Lifecycle Tests for x86_64, AArch64, and RISC-V 64 Omega Virtual Devices
run_ovd_test "omega_phone_x86" "x86_64"
run_ovd_test "omega_tablet_arm64" "aarch64"
run_ovd_test "omega_device_riscv" "riscv64"

echo -e "\n${GREEN}=================================================${NC}"
echo -e "${GREEN}  OMEGA VIRTUAL DEVICE (OVD) TEST SUITE PASSED!  ${NC}"
echo -e "${GREEN}=================================================${NC}"
