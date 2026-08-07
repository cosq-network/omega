#!/usr/bin/env bash

# Omega Virtual Device (OVD) Launcher Script
# x86_64: Standard VGA (Bochs VBE) with optional SDL/Cocoa window or headless console
# AArch64 / RISC-V: SimpleFb/serial fallback; --gpu requests experimental VirtIO-GPU
# Storage profiles expose the OVD image through the selected QEMU transport.

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OVD_DIR="${PROJECT_ROOT}/emulator/ovd"
BUILD_DIR="${PROJECT_ROOT}/build"

usage() {
    echo "Usage: $0 run --name <ovd_name> [--gpu|--no-gpu] [--storage <profile>] [--dry-run]"
    echo ""
    echo "  --gpu     x86_64: Standard VGA GUI; ARM/RISC-V: experimental virtio-gpu request"
    echo "  --no-gpu  headless boot; ARM/RISC-V use SimpleFb when handed off, else serial fallback"
    echo "  --storage virtio|ahci|usb|sd|optical|none  select the emulated storage protocol"
    echo "  --dry-run  print the QEMU command without starting it"
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
DRY_RUN=false
STORAGE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --name)   NAME="$2"; shift 2 ;;
        --gpu)    GPU=true; shift ;;
        --no-gpu) GPU=false; shift ;;
        --storage) STORAGE="$2"; shift 2 ;;
        --dry-run) DRY_RUN=true; shift ;;
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
CONFIG_STORAGE=$(grep "^ovd.storage=" "${CONFIG_FILE}" | cut -d'=' -f2 || true)
CONFIG_STORAGE_IMAGE=$(grep "^ovd.storage.image=" "${CONFIG_FILE}" | cut -d'=' -f2 || true)
CONFIG_STORAGE_READONLY=$(grep "^ovd.storage.readonly=" "${CONFIG_FILE}" | cut -d'=' -f2 || true)
[ -n "${STORAGE}" ] || STORAGE="${CONFIG_STORAGE:-auto}"
STORAGE_IMAGE="${OVD_PATH}/${CONFIG_STORAGE_IMAGE:-userdata.img}"

case "${STORAGE}" in
    auto|virtio|ahci|usb|sd|optical|none) ;;
    *) echo -e "${RED}[ERROR] Unsupported storage profile '${STORAGE}'.${NC}"; usage ;;
esac

if [ "${CONFIG_STORAGE_READONLY:-false}" = "true" ]; then
    STORAGE_READONLY=true
else
    STORAGE_READONLY=false
fi

KERNEL_ELF="${BUILD_DIR}/${ARCH}/omega.elf"
if [ ! -f "${KERNEL_ELF}" ]; then
    echo "[*] Building kernel binary for ${ARCH}..."
    mkdir -p "${BUILD_DIR}/${ARCH}" && cd "${BUILD_DIR}/${ARCH}"
    cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/${ARCH}-toolchain.cmake -DARCH="${ARCH}" ../.. > /dev/null
    make > /dev/null
    cd "${PROJECT_ROOT}"
fi

echo -e "[*] Launching Omega Virtual Device '${GREEN}${NAME}${NC}' (${ARCH}, RAM: ${RAM}MB, GPU: ${GPU})..."

add_storage_args() {
    local profile="${STORAGE}"
    local drive_opts="file=${STORAGE_IMAGE},format=raw,if=none,id=storage0"
    [ "${STORAGE_READONLY}" = true ] && drive_opts+=",readonly=on"

    case "${profile}" in
        auto)
            QEMU_ARGS+=("-drive" "file=${STORAGE_IMAGE},format=raw,index=0,media=disk")
            ;;
        virtio)
            QEMU_ARGS+=("-drive" "${drive_opts}" "-device")
            if [ "${ARCH}" = "x86_64" ]; then
                QEMU_ARGS+=("virtio-blk-pci,drive=storage0")
            else
                QEMU_ARGS+=("virtio-blk-device,drive=storage0")
            fi
            ;;
        ahci)
            if [ "${ARCH}" != "x86_64" ]; then
                echo -e "${RED}[ERROR] AHCI storage is currently supported by the emulator profile only on x86_64.${NC}" >&2
                exit 1
            fi
            QEMU_ARGS+=("-drive" "file=${STORAGE_IMAGE},format=raw,if=ide,index=0,media=disk")
            ;;
        usb)
            QEMU_ARGS+=("-drive" "${drive_opts}" "-device" "usb-storage,drive=storage0")
            ;;
        sd)
            QEMU_ARGS+=("-drive" "file=${STORAGE_IMAGE},format=raw,if=sd,index=0,media=disk")
            ;;
        optical)
            QEMU_ARGS+=("-drive" "file=${STORAGE_IMAGE},format=raw,if=none,id=storage0,media=cdrom,readonly=on" "-device" "ide-cd,drive=storage0")
            ;;
        none) ;;
    esac
}

run_qemu() {
    printf '[*] QEMU command:'
    printf ' %q' "$@"
    printf '\n'
    if [ "${DRY_RUN}" = true ]; then
        return 0
    fi
    exec "$@"
}

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

resolve_gui_backend() {
    case "$(uname -s)" in
        Darwin) echo "cocoa" ;;
        Linux)  [ -n "${DISPLAY:-}" ] && echo "sdl" || echo "none" ;;
        *)      echo "none" ;;
    esac
}

case "${ARCH}" in
    x86_64)
        QEMU_ARGS=(
            -m "${RAM}"
            -kernel "${KERNEL_ELF}"
            -serial stdio
            -vga std
            -display "$(resolve_x86_display_backend)"
        )
        add_storage_args
        if [ "${GPU}" = true ]; then
            echo "[*] x86_64 display: Standard VGA (Bochs VBE 1024x768) with GUI window"
        else
            echo "[*] x86_64 display: Standard VGA (Bochs VBE) headless — serial mirrors kprintf"
        fi
        run_qemu qemu-system-x86_64 "${QEMU_ARGS[@]}"
        ;;

    aarch64)
        if [ "${GPU}" = true ]; then
            DISPLAY_BACKEND="$(resolve_gui_backend)"
            echo "[*] AArch64: experimental VirtIO-GPU request (${DISPLAY_BACKEND}); kernel falls back safely if unavailable"
            QEMU_ARGS=(-M virt -cpu cortex-a57 -m "${RAM}" \
                -kernel "${KERNEL_ELF}" \
                -serial stdio -device virtio-gpu-pci -display "${DISPLAY_BACKEND}")
            add_storage_args
            run_qemu qemu-system-aarch64 "${QEMU_ARGS[@]}"
        fi
        echo "[*] AArch64: SimpleFb if firmware provides a DT framebuffer; otherwise serial fallback"
        QEMU_ARGS=(-M virt -cpu cortex-a57 -m "${RAM}" \
            -kernel "${KERNEL_ELF}" \
            -nographic)
        add_storage_args
        run_qemu qemu-system-aarch64 "${QEMU_ARGS[@]}"
        ;;

    riscv64)
        if [ "${GPU}" = true ]; then
            DISPLAY_BACKEND="$(resolve_gui_backend)"
            echo "[*] RISC-V 64: experimental VirtIO-GPU request (${DISPLAY_BACKEND}); kernel falls back safely if unavailable"
            QEMU_ARGS=(-M virt -cpu rv64 -bios default -m "${RAM}" \
                -kernel "${KERNEL_ELF}" \
                -serial stdio -device virtio-gpu-pci -display "${DISPLAY_BACKEND}")
            add_storage_args
            run_qemu qemu-system-riscv64 "${QEMU_ARGS[@]}"
        fi
        echo "[*] RISC-V 64: SimpleFb if firmware provides a DT framebuffer; otherwise serial fallback"
        QEMU_ARGS=(-M virt -cpu rv64 -bios default -m "${RAM}" \
            -kernel "${KERNEL_ELF}" \
            -nographic)
        add_storage_args
        run_qemu qemu-system-riscv64 "${QEMU_ARGS[@]}"
        ;;

    *)
        echo -e "${RED}[ERROR] Unsupported architecture: ${ARCH}${NC}"
        exit 1
        ;;
esac
