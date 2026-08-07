#!/usr/bin/env bash

# Omega Virtual Device (OVD) Launcher Script
# x86_64: Standard VGA (Bochs VBE) with optional SDL/Cocoa window or headless serial console
# AArch64 / RISC-V: serial console only (display HAL stubs until Phase 9)

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OVD_DIR="${PROJECT_ROOT}/emulator/ovd"
BUILD_DIR="${PROJECT_ROOT}/build"

usage() {
    echo "Usage: $0 run --name <ovd_name> [--gpu|--no-gpu]"
    echo ""
    echo "  --gpu     x86_64: -vga std with a GUI window (SDL on Linux, Cocoa on macOS)"
    echo "  --no-gpu  x86_64: -vga std -display none (Bochs VBE headless, serial log)"
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
    cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/${ARCH}-toolchain.cmake -DARCH="${ARCH}" ../.. > /dev/null
    make > /dev/null
    cd "${PROJECT_ROOT}"
fi

echo -e "[*] Launching Omega Virtual Device '${GREEN}${NAME}${NC}' (${ARCH}, RAM: ${RAM}MB, GPU: ${GPU})..."

resolve_x86_display_backend() {
    if [ "${GPU}" != true ]; then
        echo "none"
        return
    fi

    case "$(uname -s)" in
        Darwin)
            echo "cocoa"
            ;;
        Linux)
            if [ -n "${DISPLAY:-}" ]; then
                echo "sdl"
            else
                echo -e "${YELLOW}[!] DISPLAY unset; falling back to headless (-display none).${NC}" >&2
                echo "none"
            fi
            ;;
        *)
            echo -e "${YELLOW}[!] Unknown host OS; using headless display backend.${NC}" >&2
            echo "none"
            ;;
    esac
}

case "${ARCH}" in
    x86_64)
        QEMU_ARGS=(
            -m "${RAM}"
            -kernel "${KERNEL_ELF}"
            -drive "file=${OVD_PATH}/userdata.img,format=raw,index=0,media=disk"
            -serial stdio
            -vga std
            -display "$(resolve_x86_display_backend)"
        )
        if [ "${GPU}" = true ]; then
            echo "[*] x86_64 display: Standard VGA (Bochs VBE 1024x768) with GUI window"
        else
            echo "[*] x86_64 display: Standard VGA (Bochs VBE) headless — serial mirrors kprintf"
        fi
        exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
        ;;

    aarch64)
        echo "[*] AArch64: serial console (display HAL stub — no VGA)"
        exec qemu-system-aarch64 \
            -M virt -cpu cortex-a57 \
            -m "${RAM}" \
            -kernel "${KERNEL_ELF}" \
            -drive "file=${OVD_PATH}/userdata.img,format=raw,index=0,media=disk" \
            -nographic
        ;;

    riscv64)
        echo "[*] RISC-V 64: serial console (display HAL stub — no VGA)"
        exec qemu-system-riscv64 \
            -M virt -cpu rv64 -bios default \
            -m "${RAM}" \
            -kernel "${KERNEL_ELF}" \
            -drive "file=${OVD_PATH}/userdata.img,format=raw,index=0,media=disk" \
            -nographic
        ;;

    *)
        echo -e "${RED}[ERROR] Unsupported architecture: ${ARCH}${NC}"
        exit 1
        ;;
esac
