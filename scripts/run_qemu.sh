#!/usr/bin/env bash

# Quick x86_64 QEMU launcher. For named devices and lifecycle state, use the
# OVD launcher under emulator/ovd_run.sh instead.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${OMEGA_BUILD_ROOT:-${PROJECT_ROOT}/build}"
IMAGE_ROOT="${OMEGA_IMAGE_ROOT:-${PROJECT_ROOT}/disk_images}"
BUILD_DIR="${BUILD_ROOT}/x86_64"
KERNEL="${BUILD_DIR}/omega.elf"
DEFAULT_IMAGE="${IMAGE_ROOT}/omega-x86_64-bootable.img"

MODE=headless
STORAGE=auto
STORAGE_IMAGE="${DEFAULT_IMAGE}"
NETWORK=none
INITRD=""
READONLY=false
EPHEMERAL=false
QMP=false
DRY_RUN=false
NO_BUILD=false

usage() {
    cat <<USAGE
Usage: $0 [options]
  --gui | --text | --headless     Select display mode (default: headless)
  --storage PROFILE               auto|virtio|ahci|usb|none
  --storage-image FILE            Backing image override
  --network PROFILE               none|user|socket
  --initrd FILE                   Attach an initrd
  --readonly                      Open storage read-only
  --ephemeral                     Discard disk writes with QEMU -snapshot
  --qmp                           Enable a QMP socket under build/x86_64
  --dry-run                       Print the command without starting QEMU
  --no-build                      Fail if the kernel ELF is missing
USAGE
    exit 1
}

while (($#)); do
    case "$1" in
        --gui) MODE=gui; shift;; --text) MODE=text; shift;; --headless) MODE=headless; shift;;
        --storage) STORAGE="${2:-}"; shift 2;; --storage-image) STORAGE_IMAGE="${2:-}"; shift 2;;
        --network) NETWORK="${2:-}"; shift 2;; --initrd) INITRD="${2:-}"; shift 2;;
        --readonly) READONLY=true; shift;; --ephemeral) EPHEMERAL=true; shift;;
        --qmp) QMP=true; shift;; --dry-run) DRY_RUN=true; shift;; --no-build) NO_BUILD=true; shift;;
        *) usage;;
    esac
done

case "${STORAGE}" in auto|virtio|ahci|usb|none) ;; *) echo "Unsupported storage profile: ${STORAGE}" >&2; exit 1;; esac
case "${NETWORK}" in none|user|socket) ;; *) echo "Unsupported network profile: ${NETWORK}" >&2; exit 1;; esac
[ -z "${INITRD}" ] || [ -f "${INITRD}" ] || { echo "Initrd not found: ${INITRD}" >&2; exit 1; }

if [ ! -f "${KERNEL}" ]; then
    [ "${NO_BUILD}" = false ] || { echo "Kernel not found: ${KERNEL}" >&2; exit 1; }
    command -v cmake >/dev/null 2>&1 || { echo "Required command not found: cmake" >&2; exit 1; }
    echo "[*] Building x86_64 kernel..."
    cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
        -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/x86_64-toolchain.cmake" \
        -DARCH=x86_64
    cmake --build "${BUILD_DIR}"
fi

QEMU_ARGS=( -name omega-quick-x86_64 -kernel "${KERNEL}" -serial stdio )
case "${MODE}" in
    gui)
        case "$(uname -s)" in Darwin) backend=cocoa;; Linux) backend="${DISPLAY:+sdl}"; backend="${backend:-none}";; *) backend=none;; esac
        QEMU_ARGS+=( -vga std -display "${backend}" )
        ;;
    text) QEMU_ARGS+=( -display none -vga none );;
    headless) QEMU_ARGS+=( -display none -vga std );;
esac

if [ "${STORAGE}" != none ]; then
    [ "${READONLY}" = true ] && readonly_arg=",readonly=on" || readonly_arg=""
    case "${STORAGE}" in
        auto) QEMU_ARGS+=( -drive "file=${STORAGE_IMAGE},format=raw,index=0,media=disk${readonly_arg}" );;
        virtio) QEMU_ARGS+=( -drive "file=${STORAGE_IMAGE},format=raw,if=none,id=storage0${readonly_arg}" -device virtio-blk-pci,drive=storage0 );;
        ahci) QEMU_ARGS+=( -drive "file=${STORAGE_IMAGE},format=raw,if=ide,index=0,media=disk${readonly_arg}" );;
        usb) QEMU_ARGS+=( -drive "file=${STORAGE_IMAGE},format=raw,if=none,id=storage0${readonly_arg}" -device usb-storage,drive=storage0 );;
    esac
fi
case "${NETWORK}" in
    user) QEMU_ARGS+=( -netdev user,id=net0 -device virtio-net-pci,netdev=net0 );;
    socket) QEMU_ARGS+=( -netdev socket,id=net0,listen=unix:"${BUILD_DIR}/network.sock" -device virtio-net-pci,netdev=net0 );;
esac
[ -z "${INITRD}" ] || QEMU_ARGS+=( -initrd "${INITRD}" )
[ "${EPHEMERAL}" = true ] && QEMU_ARGS+=( -snapshot )
if [ "${QMP}" = true ]; then
    mkdir -p "${BUILD_DIR}"; rm -f "${BUILD_DIR}/quick.qmp.sock"
    QEMU_ARGS+=( -qmp "unix:${BUILD_DIR}/quick.qmp.sock,server=on,wait=off" )
fi

printf '[*] QEMU command:'; printf ' %q' qemu-system-x86_64 "${QEMU_ARGS[@]}"; printf '\n'
[ "${DRY_RUN}" = true ] && exit 0
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "Required command not found: qemu-system-x86_64" >&2; exit 1; }
exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
