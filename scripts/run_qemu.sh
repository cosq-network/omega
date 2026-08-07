#!/usr/bin/env bash

# Quick QEMU launcher for Omega x86_64 kernel with Standard VGA support.
# Usage:
#   ./scripts/run_qemu.sh              # headless Bochs VBE (-vga std -display none)
#   ./scripts/run_qemu.sh --gui        # graphical window (SDL/Cocoa)
#   ./scripts/run_qemu.sh --text       # VGA text mode only (-vga none)
#   ./scripts/run_qemu.sh --storage virtio|ahci|usb|none

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/x86_64"
KERNEL="${BUILD_DIR}/omega.elf"

MODE="headless"
STORAGE="auto"
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --gui)  MODE="gui"; shift ;;
        --text) MODE="text"; shift ;;
        --storage) STORAGE="$2"; shift 2 ;;
        --dry-run) DRY_RUN=true; shift ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

if [ ! -f "${KERNEL}" ]; then
    echo "[*] Building x86_64 kernel..."
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/x86_64-toolchain.cmake -DARCH=x86_64 ../.. > /dev/null
    make > /dev/null
fi

case "${STORAGE}" in
    auto|virtio|ahci|usb|none) ;;
    *) echo "Unsupported storage profile: ${STORAGE}"; exit 1 ;;
esac

add_storage_args() {
    case "${STORAGE}" in
        auto) QEMU_ARGS+=("-drive" "file=${PROJECT_ROOT}/disk_images/omega-x86_64-bootable.img,format=raw,index=0,media=disk") ;;
        virtio) QEMU_ARGS+=("-drive" "file=${PROJECT_ROOT}/disk_images/omega-x86_64-bootable.img,format=raw,if=none,id=storage0" "-device" "virtio-blk-pci,drive=storage0") ;;
        ahci) QEMU_ARGS+=("-drive" "file=${PROJECT_ROOT}/disk_images/omega-x86_64-bootable.img,format=raw,if=ide,index=0,media=disk") ;;
        usb) QEMU_ARGS+=("-drive" "file=${PROJECT_ROOT}/disk_images/omega-x86_64-bootable.img,format=raw,if=none,id=storage0" "-device" "usb-storage,drive=storage0") ;;
        none) ;;
    esac
}

run_qemu() {
    printf '[*] QEMU command:'
    printf ' %q' "$@"
    printf '\n'
    [ "${DRY_RUN}" = true ] || exec "$@"
}

case "${MODE}" in
    gui)
        case "$(uname -s)" in
            Darwin) DISPLAY_BACKEND="cocoa" ;;
            *)      DISPLAY_BACKEND="sdl" ;;
        esac
        echo "[*] Launching with Standard VGA GUI (${DISPLAY_BACKEND})..."
        QEMU_ARGS=(
            -kernel "${KERNEL}" \
            -serial stdio \
            -vga std \
            -display "${DISPLAY_BACKEND}"
        )
        add_storage_args
        run_qemu qemu-system-x86_64 "${QEMU_ARGS[@]}"
        ;;
    text)
        echo "[*] Launching with VGA text mode only (-vga none)..."
        QEMU_ARGS=(
            -kernel "${KERNEL}" \
            -serial stdio \
            -display none \
            -vga none
        )
        add_storage_args
        run_qemu qemu-system-x86_64 "${QEMU_ARGS[@]}"
        ;;
    headless)
        echo "[*] Launching headless with Bochs VBE (-vga std -display none)..."
        QEMU_ARGS=(
            -kernel "${KERNEL}" \
            -serial stdio \
            -display none \
            -vga std
        )
        add_storage_args
        run_qemu qemu-system-x86_64 "${QEMU_ARGS[@]}"
        ;;
esac
