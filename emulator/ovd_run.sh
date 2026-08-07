#!/usr/bin/env bash

set -euo pipefail
# AArch64/RISC-V use the SimpleFb device-tree path when firmware provides it;
# --gpu requests the experimental VirtIO-GPU device.
# Keep the explicit model text here for static tooling and user guidance.
# SimpleFb fallback; -device virtio-gpu-pci is opt-in display wiring.
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=libovd.sh
source "$(dirname "${BASH_SOURCE[0]}")/libovd.sh"

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; NC='\033[0m'

usage() {
    cat <<USAGE
Usage: $0 run --name NAME [options]
  --gpu | --no-gpu             Select display behavior
  --storage PROFILE            Override configured storage profile
  --storage-image FILE         Override configured backing image
  --network none|user|socket   Override configured network profile
  --initrd FILE                Attach an initrd image
  --readonly                   Open the storage image read-only
  --ephemeral                  Do not persist disk writes (-snapshot)
  --qmp                        Enable the QMP monitor socket for a foreground launch
  --no-qmp                     Disable the per-OVD QMP monitor socket
  --daemon                     Start in background and write state/qemu.pid
  --dry-run                    Print the exact command without starting QEMU
USAGE
    exit 1
}

[ "${1:-}" = run ] || usage
shift
NAME=''; GPU='auto'; STORAGE=''; STORAGE_IMAGE=''; STORAGE_IMAGE_OVERRIDE=''; NETWORK=''; INITRD=''; READONLY=false; EPHEMERAL=false; DAEMON=false; DRY_RUN=false; QMP=auto
while (($#)); do
    case "$1" in
        --name) NAME="${2:-}"; shift 2;; --gpu) GPU=true; shift;; --no-gpu) GPU=false; shift;;
        --storage) STORAGE="${2:-}"; shift 2;; --storage-image) STORAGE_IMAGE="${2:-}"; STORAGE_IMAGE_OVERRIDE=true; shift 2;;
        --network) NETWORK="${2:-}"; shift 2;; --initrd) INITRD="${2:-}"; shift 2;;
        --readonly) READONLY=true; shift;; --ephemeral) EPHEMERAL=true; shift;; --qmp) QMP=true; shift;; --no-qmp) QMP=false; shift;;
        --daemon) DAEMON=true; shift;; --dry-run) DRY_RUN=true; shift;; *) usage;;
    esac
done
ovd_validate_name "${NAME}"
OVD_PATH="$(ovd_path "${NAME}")"; CONFIG_FILE="${OVD_PATH}/config.ini"
[ -f "${CONFIG_FILE}" ] || ovd_error "OVD '${NAME}' does not exist."
if [ "${STORAGE_IMAGE_OVERRIDE}" = true ]; then OVD_SKIP_IMAGE_CHECK=true ovd_validate_config "${NAME}"; else ovd_validate_config "${NAME}"; fi
ARCH="$(ovd_config_value "${CONFIG_FILE}" arch)"; RAM="$(ovd_config_value "${CONFIG_FILE}" ram)"; DISPLAY_PROFILE="$(ovd_config_value "${CONFIG_FILE}" display)"
PROFILE_ID="$(ovd_config_value "${CONFIG_FILE}" profile_id)"
[ -n "${STORAGE}" ] || STORAGE="$(ovd_config_value "${CONFIG_FILE}" storage)"
[ -n "${NETWORK}" ] || NETWORK="$(ovd_config_value "${CONFIG_FILE}" network)"
[ "${STORAGE_IMAGE_OVERRIDE}" = true ] || STORAGE_IMAGE="$(ovd_config_value "${CONFIG_FILE}" image)"
[ -n "${INITRD}" ] || INITRD="$(ovd_config_value "${CONFIG_FILE}" initrd)"
ovd_validate_profile "${STORAGE}"; ovd_profile_supported "${ARCH}" "${STORAGE}" || ovd_error "Storage profile '${STORAGE}' is unavailable on ${ARCH}."
ovd_validate_network "${NETWORK}"
[ -z "${INITRD}" ] || [ -f "${INITRD}" ] || ovd_error "Initrd not found: ${INITRD}"
if ovd_pid_running "${NAME}"; then ovd_error "OVD '${NAME}' is already running."; fi
if [ -n "${PROFILE_ID}" ]; then
    PROFILE_TOOL="${PROJECT_ROOT}/emulator/profile_catalog.py"
    [ -f "${PROFILE_TOOL}" ] || ovd_error "Profile catalog tool is missing: ${PROFILE_TOOL}"
    python3 "${PROFILE_TOOL}" artifacts --profile "${PROFILE_ID}" >/dev/null
    PROFILE_IMAGE="${OVD_IMAGE_ROOT}/omega-${PROFILE_ID}-ext4.raw"
    [ -f "${PROFILE_IMAGE}" ] || ovd_error "Resolved profile image is missing: ${PROFILE_IMAGE}"
    PROFILE_LOCAL_IMAGE="${OVD_PATH}/$(ovd_config_value "${CONFIG_FILE}" image)"
    if [ "${STORAGE_IMAGE_OVERRIDE}" != true ] && ! cmp -s "${PROFILE_IMAGE}" "${PROFILE_LOCAL_IMAGE}"; then
        cp "${PROFILE_IMAGE}" "${PROFILE_LOCAL_IMAGE}"
    fi
fi
if [ "${STORAGE_IMAGE_OVERRIDE}" = true ]; then STORAGE_PATH="${STORAGE_IMAGE}"; else [[ "${STORAGE_IMAGE}" != /* && "${STORAGE_IMAGE}" != *..* ]] || ovd_error "Configured storage image must remain inside the OVD directory."; STORAGE_PATH="${OVD_PATH}/${STORAGE_IMAGE}"; fi
[ -f "${STORAGE_PATH}" ] || ovd_error "Storage image not found: ${STORAGE_PATH}"
if [ "$(ovd_config_value "${CONFIG_FILE}" readonly)" = true ]; then READONLY=true; fi
if [ "${GPU}" = auto ]; then
    case "${DISPLAY_PROFILE}" in
        standard-vga|virtio-gpu) GPU=true;;
        simplefb|none|'') GPU=false;;
    esac
fi

KERNEL_ELF="${OVD_BUILD_ROOT}/${ARCH}/omega.elf"
if [ ! -f "${KERNEL_ELF}" ]; then
    ovd_require_command cmake; ovd_require_command make
    mkdir -p "$(dirname "${KERNEL_ELF}")"
    echo "[*] Building kernel binary for ${ARCH}..."
    cmake -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/${ARCH}-toolchain.cmake" -DARCH="${ARCH}" -S "${PROJECT_ROOT}" -B "${OVD_BUILD_ROOT}/${ARCH}"
    cmake --build "${OVD_BUILD_ROOT}/${ARCH}"
fi

STATE_DIR="$(ovd_state_dir "${NAME}")"; mkdir -p "${STATE_DIR}"
PID_FILE="$(ovd_pid_file "${NAME}")"; LOG_FILE="$(ovd_log_file "${NAME}")"; COMMAND_FILE="$(ovd_command_file "${NAME}")"

QEMU_ARGS=()
resolve_x86_display() {
    [ "${GPU}" = true ] || { echo none; return; }
    case "$(uname -s)" in
        Darwin) echo cocoa;;
        Linux) [ -n "${DISPLAY:-}" ] && echo sdl || echo none;;
        *) echo none;;
    esac
}
add_storage() {
    local drive="file=${STORAGE_PATH},format=raw,if=none,id=storage0"
    [ "${READONLY}" = true ] && drive+=",readonly=on"
    case "${STORAGE}" in
        auto) QEMU_ARGS+=( -drive "file=${STORAGE_PATH},format=raw,index=0,media=disk" );;
        virtio) QEMU_ARGS+=( -drive "${drive}" -device "$([ "${ARCH}" = x86_64 ] && echo 'virtio-blk-pci,disable-modern=on' || echo virtio-blk-device),drive=storage0" );;
        ahci) QEMU_ARGS+=( -drive "file=${STORAGE_PATH},format=raw,if=ide,index=0,media=disk" );;
        usb) QEMU_ARGS+=( -drive "${drive}" -device usb-storage,drive=storage0 );;
        sd) QEMU_ARGS+=( -drive "file=${STORAGE_PATH},format=raw,if=sd,index=0,media=disk" );;
        optical) QEMU_ARGS+=( -drive "file=${STORAGE_PATH},format=raw,if=none,id=storage0,media=cdrom,readonly=on" -device ide-cd,drive=storage0 );;
        none) ;;
    esac
}
add_network() {
    case "${NETWORK}" in
        none) ;;
        user) QEMU_ARGS+=( -netdev user,id=net0 -device "$([ "${ARCH}" = x86_64 ] && echo virtio-net-pci || echo virtio-net-device),netdev=net0" );;
        socket) QEMU_ARGS+=( -netdev socket,id=net0,listen=unix:"${STATE_DIR}/network.sock" -device "$([ "${ARCH}" = x86_64 ] && echo virtio-net-pci || echo virtio-net-device),netdev=net0" );;
    esac
}
if [ "${ARCH}" = x86_64 ]; then
    DISPLAY="${OVD_DISPLAY_BACKEND:-$(resolve_x86_display)}"
    QEMU_ARGS=( -name "omega-${NAME}" -m "${RAM}" -kernel "${KERNEL_ELF}" -serial stdio -vga std -display "${DISPLAY}" )
elif [ "${ARCH}" = aarch64 ]; then
    if [ "${GPU}" = true ]; then QEMU_ARGS=( -name "omega-${NAME}" -M virt -cpu cortex-a57 -m "${RAM}" -kernel "${KERNEL_ELF}" -serial stdio -device virtio-gpu-pci -display "${OVD_DISPLAY_BACKEND:-none}" ); else QEMU_ARGS=( -name "omega-${NAME}" -M virt -cpu cortex-a57 -m "${RAM}" -kernel "${KERNEL_ELF}" -nographic ); fi
else
    if [ "${GPU}" = true ]; then QEMU_ARGS=( -name "omega-${NAME}" -M virt -cpu rv64 -bios default -m "${RAM}" -kernel "${KERNEL_ELF}" -serial stdio -device virtio-gpu-pci -display "${OVD_DISPLAY_BACKEND:-none}" ); else QEMU_ARGS=( -name "omega-${NAME}" -M virt -cpu rv64 -bios default -m "${RAM}" -kernel "${KERNEL_ELF}" -nographic ); fi
fi
add_storage; add_network
[ -z "${INITRD}" ] || QEMU_ARGS+=( -initrd "${INITRD}" )
[ "${EPHEMERAL}" = true ] && QEMU_ARGS+=( -snapshot )
QMP_SOCKET="$(ovd_qmp_socket "${NAME}")"; rm -f "${QMP_SOCKET}"
[ "${QMP}" = auto ] && QMP="${DAEMON}"
[ "${QMP}" = true ] && QEMU_ARGS+=( -qmp "unix:${QMP_SOCKET},server=on,wait=off" )
QEMU_BIN="qemu-system-${ARCH}"
printf '%q\n' "${QEMU_BIN}" "${QEMU_ARGS[@]}" > "${COMMAND_FILE}"
echo -e "[*] OVD '${GREEN}${NAME}${NC}' (${ARCH}) storage=${STORAGE} network=${NETWORK}"
echo -n "[*] QEMU command:"; printf ' %q' "${QEMU_BIN}" "${QEMU_ARGS[@]}"; echo
if [ "${DRY_RUN}" = true ]; then exit 0; fi
ovd_require_command "${QEMU_BIN}"

if [ "${DAEMON}" = true ]; then
    nohup "${QEMU_BIN}" "${QEMU_ARGS[@]}" >"${LOG_FILE}" 2>&1 < /dev/null &
    echo $! > "${PID_FILE}"
    echo "[+] Started '${NAME}' in background (PID $(cat "${PID_FILE}"))."
else
    echo "$$" > "${PID_FILE}"
    trap 'rm -f "${PID_FILE}"' EXIT
    "${QEMU_BIN}" "${QEMU_ARGS[@]}" 2>&1 | tee -a "${LOG_FILE}"
fi
