#!/usr/bin/env bash

# Omega Virtual Device (OVD) Manager & Creator
# Compatible with macOS and Linux across x86_64, AArch64, and RISC-V 64 architectures

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OVD_DIR="${PROJECT_ROOT}/emulator/ovd"

mkdir -p "${OVD_DIR}"

usage() {
    echo "Usage: $0 create --name <ovd_name> --arch <x86_64|aarch64|riscv64> [--ram <MB>] [--disk <MB>] [--storage <virtio|ahci|usb|sd|optical|none>]"
    echo "       $0 list"
    echo "       $0 delete --name <ovd_name>"
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

COMMAND=$1
shift

case "${COMMAND}" in
    create)
        NAME=""
        ARCH="x86_64"
        RAM="1024"
        DISK="64"
        STORAGE="virtio"

        while [[ $# -gt 0 ]]; do
            case $1 in
                --name) NAME="$2"; shift 2 ;;
                --arch) ARCH="$2"; shift 2 ;;
                --ram)  RAM="$2";  shift 2 ;;
                --disk) DISK="$2"; shift 2 ;;
                --storage) STORAGE="$2"; shift 2 ;;
                *) usage ;;
            esac
        done

        if [ -z "${NAME}" ]; then
            echo -e "${RED}[ERROR] OVD Name is required.${NC}"
            usage
        fi

        case "${STORAGE}" in
            virtio|ahci|usb|sd|optical|none) ;;
            *) echo -e "${RED}[ERROR] Unsupported storage profile '${STORAGE}'.${NC}"; usage ;;
        esac

        OVD_PATH="${OVD_DIR}/${NAME}"
        if [ -d "${OVD_PATH}" ]; then
            echo -e "${RED}[ERROR] Omega Virtual Device '${NAME}' already exists.${NC}"
            exit 1
        fi

        echo -e "[*] Creating Omega Virtual Device '${GREEN}${NAME}${NC}' (${ARCH})..."
        mkdir -p "${OVD_PATH}"

        # Write OVD Configuration file
        cat << EOF > "${OVD_PATH}/config.ini"
ovd.name=${NAME}
ovd.arch=${ARCH}
ovd.ram=${RAM}
ovd.disk=${DISK}
ovd.storage=${STORAGE}
ovd.storage.image=userdata.img
ovd.storage.readonly=false
ovd.vga=$([ "${ARCH}" = "x86_64" ] && echo "std" || echo "simplefb")
EOF

        # Create virtual storage disk image
        dd if=/dev/zero of="${OVD_PATH}/userdata.img" bs=1M count=${DISK} status=none
        echo -e "${GREEN}[+] Successfully created Omega Virtual Device '${NAME}' at: ${OVD_PATH}${NC}"
        ;;

    list)
        echo "========================================="
        echo "   Omega Virtual Devices (OVD List)      "
        echo "========================================="
        if [ -z "$(ls -A "${OVD_DIR}")" ]; then
            echo "No Omega Virtual Devices configured."
        else
            for d in "${OVD_DIR}"/*; do
                if [ -d "$d" ] && [ -f "$d/config.ini" ]; then
                    echo "Device: $(basename "$d")"
                    cat "$d/config.ini" | sed 's/^/  /'
                    echo "-----------------------------------------"
                fi
            done
        fi
        ;;

    delete)
        NAME=""
        while [[ $# -gt 0 ]]; do
            case $1 in
                --name) NAME="$2"; shift 2 ;;
                *) usage ;;
            esac
        done

        if [ -z "${NAME}" ]; then
            echo -e "${RED}[ERROR] OVD Name is required.${NC}"
            usage
        fi

        OVD_PATH="${OVD_DIR}/${NAME}"
        if [ -d "${OVD_PATH}" ]; then
            rm -rf "${OVD_PATH}"
            echo -e "${GREEN}[+] Successfully deleted Omega Virtual Device '${NAME}'.${NC}"
        else
            echo -e "${RED}[ERROR] Omega Virtual Device '${NAME}' not found.${NC}"
            exit 1
        fi
        ;;

    *)
        usage
        ;;
esac
