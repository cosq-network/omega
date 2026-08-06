#!/usr/bin/env bash

# Omega Virtual Device (OVD) Launcher Script (with and without graphics support)
# Compatible with macOS and Linux across x86_64, AArch64, and RISC-V 64 architectures

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OVD_DIR="${PROJECT_ROOT}/emulator/ovd"
BUILD_DIR="${PROJECT_ROOT}/build"

usage() {
    echo "Usage: $0 run --name <ovd_name> [--gpu|--no-gpu]"
    exit 1
}

if [ $# -lt 1 ]; then
    usage
fi

COMMAND=$1
shift

if [ "${COMMAND}" != "run" ]; then
    usage
fi

NAME=""
GPU=true

while [[ $# -gt 0 ]]; do
    case $1 in
        --name)   NAME="$2"; shift 2 ;;
        --gpu)    GPU=true; shift ;;
        --no-gpu) GPU=false; shift ;;
        *) usage ;;
    esac
done

if [ -z "${NAME}" ]; then
    echo -e "${RED}[ERROR] OVD Name is required.${NC}"
    usage
fi

OVD_PATH="${OVD_DIR}/${NAME}"
CONFIG_FILE="${OVD_PATH}/config.ini"

if [ ! -f "${CONFIG_FILE}" ]; then
    echo -e "${RED}[ERROR] Omega Virtual Device '${NAME}' does not exist.${NC}"
    exit 1
fi

ARCH=$(grep "^ovd.arch=" "${CONFIG_FILE}" | cut -d'=' -f2)
RAM=$(grep "^ovd.ram=" "${CONFIG_FILE}" | cut -d'=' -f2)

KERNEL_ELF="${BUILD_DIR}/${ARCH}/omega.elf"
if [ ! -f "${KERNEL_ELF}" ]; then
    echo "[*] Building kernel binary for ${ARCH}..."
    mkdir -p "${BUILD_DIR}/${ARCH}" && cd "${BUILD_DIR}/${ARCH}"
    cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/${ARCH}-toolchain.cmake -DARCH=${ARCH} ../.. > /dev/null
    make > /dev/null
    cd "${PROJECT_ROOT}"
fi

echo -e "[*] Launching Omega Virtual Device '${GREEN}${NAME}${NC}' (${ARCH}, RAM: ${RAM}MB, GPU: ${GPU})..."

# Configure Display Mode
DISPLAY_ARGS="-serial stdio"
if [ "${GPU}" = false ]; then
    DISPLAY_ARGS="${DISPLAY_ARGS} -display none"
fi

# Execute Architecture Specific QEMU Command
case "${ARCH}" in
    x86_64)
        qemu-system-x86_64 \
            -m "${RAM}" \
            -kernel "${KERNEL_ELF}" \
            -drive file="${OVD_PATH}/userdata.img",format=raw,index=0,media=disk \
            ${DISPLAY_ARGS}
        ;;

    aarch64)
        qemu-system-aarch64 \
            -M virt -cpu cortex-a57 \
            -m "${RAM}" \
            -kernel "${KERNEL_ELF}" \
            -drive file="${OVD_PATH}/userdata.img",format=raw,index=0,media=disk \
            -nographic
        ;;

    riscv64)
        qemu-system-riscv64 \
            -M virt -cpu rv64 -bios default \
            -m "${RAM}" \
            -kernel "${KERNEL_ELF}" \
            -drive file="${OVD_PATH}/userdata.img",format=raw,index=0,media=disk \
            -nographic
        ;;

    *)
        echo -e "${RED}[ERROR] Unsupported architecture: ${ARCH}${NC}"
        exit 1
        ;;
esac
