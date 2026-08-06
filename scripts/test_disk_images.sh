#!/usr/bin/env bash

# Automated Test Suite for Omega Bootable Disk Images (scripts/create_bootable_disk.sh)
# Verifies generation of RAW (.img), QCOW2 (.qcow2), VMDK (.vmdk), VDI (.vdi) formats
# Verifies embedded payload paths ::/EFI/BOOT/ and ::/boot/ via mdir

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGES_DIR="${PROJECT_ROOT}/disk_images"
SCRIPT="${PROJECT_ROOT}/scripts/create_bootable_disk.sh"

echo "================================================="
echo "  Omega Bootable Disk Image Generator Test Suite "
echo "================================================="

# 1. Execute Disk Generator Script
echo "[*] Executing scripts/create_bootable_disk.sh..."
bash "${SCRIPT}" > /dev/null

verify_arch_image() {
    local arch=$1
    local efi_pattern=$2

    echo -e "\n[*] Testing Generated Disk Images for Architecture: ${GREEN}${arch}${NC}"

    local raw_img="${IMAGES_DIR}/omega-${arch}-bootable.img"
    local qcow2_img="${IMAGES_DIR}/omega-${arch}-bootable.qcow2"
    local vmdk_img="${IMAGES_DIR}/omega-${arch}-bootable.vmdk"
    local vdi_img="${IMAGES_DIR}/omega-${arch}-bootable.vdi"

    # Verify Disk Image Files Exist and Non-empty
    for img in "${raw_img}" "${qcow2_img}" "${vmdk_img}" "${vdi_img}"; do
        if [ -f "${img}" ] && [ -s "${img}" ]; then
            echo -e "  [PASS] Image Exists & Non-empty: $(basename "${img}")"
        else
            echo -e "  [${RED}FAIL${NC}] Missing or zero-length image: ${img}"
            exit 1
        fi
    done

    # Verify Embedded Payloads via mdir if mtools is available
    if command -v mdir &> /dev/null; then
        if mdir -i "${raw_img}" ::/EFI/BOOT | grep -i -q "${efi_pattern}"; then
            echo -e "  [PASS] Embedded Payload Verified: ::/EFI/BOOT/${efi_pattern}"
        else
            echo -e "  [${RED}FAIL${NC}] Missing ::/EFI/BOOT/${efi_pattern} in ${raw_img}"
            exit 1
        fi

        if mdir -i "${raw_img}" ::/boot | grep -i -q "omega"; then
            echo -e "  [PASS] Embedded Payload Verified: ::/boot/omega.elf"
        else
            echo -e "  [${RED}FAIL${NC}] Missing ::/boot/omega.elf in ${raw_img}"
            exit 1
        fi
    fi
}

# Verify Image Formats and Payload Structure for All Target Architectures
verify_arch_image "x86_64" "BOOTX64"
verify_arch_image "aarch64" "BOOTAA64"
verify_arch_image "riscv64" "BOOTRISCV64"

echo -e "\n${GREEN}=================================================${NC}"
echo -e "${GREEN}  DISK IMAGE GENERATOR TEST SUITE PASSED!        ${NC}"
echo -e "${GREEN}=================================================${NC}"
