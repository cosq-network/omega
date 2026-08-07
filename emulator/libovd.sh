#!/usr/bin/env bash

# Shared OVD validation, configuration, capability, and lifecycle helpers.
# This file is sourced by ovd_manager.sh and ovd_run.sh; it is not an entry
# point on its own.

OVD_PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
OVD_ROOT="${OMEGA_OVD_ROOT:-${OVD_PROJECT_ROOT}/emulator/ovd}"
OVD_BUILD_ROOT="${OMEGA_BUILD_ROOT:-${OVD_PROJECT_ROOT}/build}"
OVD_IMAGE_ROOT="${OMEGA_IMAGE_ROOT:-${OVD_PROJECT_ROOT}/disk_images}"
OVD_SCHEMA_VERSION=1

ovd_error() { echo "[ERROR] $*" >&2; return 1; }
ovd_warn() { echo "[WARN] $*" >&2; }

ovd_validate_name() {
    local value="${1:-}"
    [[ "${value}" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$ ]] || \
        ovd_error "Invalid OVD name '${value}'. Use 1-64 letters, numbers, '.', '_' or '-'."
}

ovd_validate_arch() {
    case "${1:-}" in
        x86_64|aarch64|riscv64) ;;
        *) ovd_error "Unsupported architecture '${1:-}'. Supported: x86_64, aarch64, riscv64." ;;
    esac
}

ovd_validate_uint() {
    local label="$1" value="${2:-}" minimum="${3:-1}" maximum="${4:-9223372036854775807}"
    [[ "${value}" =~ ^[0-9]+$ ]] || ovd_error "${label} must be a positive integer."
    (( value >= minimum && value <= maximum )) || \
        ovd_error "${label} must be between ${minimum} and ${maximum}."
}

ovd_validate_profile() {
    case "${1:-}" in
        auto|virtio|ahci|usb|sd|optical|none) ;;
        *) ovd_error "Unsupported storage profile '${1:-}'." ;;
    esac
}

ovd_validate_network() {
    case "${1:-}" in
        none|user|socket) ;;
        *) ovd_error "Unsupported network profile '${1:-}'. Supported: none, user, socket." ;;
    esac
}

ovd_validate_display() {
    case "${1:-}" in
        standard-vga|simplefb|virtio-gpu|none) ;;
        *) ovd_error "Unsupported display profile '${1:-}'." ;;
    esac
}

ovd_profile_supported() {
    local arch="$1" profile="$2"
    case "${arch}:${profile}" in
        x86_64:*) return 0 ;;
        aarch64:auto|riscv64:auto) return 0 ;;
        aarch64:virtio|aarch64:usb|aarch64:sd|aarch64:optical|aarch64:none) return 0 ;;
        riscv64:virtio|riscv64:usb|riscv64:sd|riscv64:optical|riscv64:none) return 0 ;;
        *) return 1 ;;
    esac
}

ovd_path() {
    ovd_validate_name "$1" || return 1
    printf '%s/%s\n' "${OVD_ROOT}" "$1"
}

ovd_config_get() {
    local file="$1" key="$2"
    awk -F= -v wanted="${key}" '$1 == wanted { sub(/^[^=]*=/, ""); print; exit }' "${file}"
}

ovd_config_get_or() {
    local file="$1" key="$2" fallback="$3" value
    value="$(ovd_config_get "${file}" "${key}" || true)"
    if [ -n "${value}" ]; then printf '%s\n' "${value}"; else printf '%s\n' "${fallback}"; fi
}

ovd_config_value() {
    local file="$1" field="$2" value
    case "${field}" in
        name) value="$(ovd_config_get_or "${file}" ovd.name "")" ;;
        arch) value="$(ovd_config_get_or "${file}" ovd.arch "")" ;;
        ram) value="$(ovd_config_get_or "${file}" ovd.ram_mb "$(ovd_config_get_or "${file}" ovd.ram "")")" ;;
        disk) value="$(ovd_config_get_or "${file}" ovd.disk_mb "$(ovd_config_get_or "${file}" ovd.disk "")")" ;;
        storage) value="$(ovd_config_get_or "${file}" ovd.storage.profile "$(ovd_config_get_or "${file}" ovd.storage "auto")")" ;;
        image) value="$(ovd_config_get_or "${file}" ovd.storage.image userdata.img)" ;;
        readonly) value="$(ovd_config_get_or "${file}" ovd.storage.readonly false)" ;;
        display) value="$(ovd_config_get_or "${file}" ovd.display.profile "")" ;;
        network) value="$(ovd_config_get_or "${file}" ovd.network.profile none)" ;;
        initrd) value="$(ovd_config_get_or "${file}" ovd.initrd "")" ;;
        profile_id) value="$(ovd_config_get_or "${file}" ovd.profile.id "")" ;;
        filesystem) value="$(ovd_config_get_or "${file}" ovd.filesystem.system "")" ;;
        boot_filesystem) value="$(ovd_config_get_or "${file}" ovd.filesystem.boot "")" ;;
        artifact_policy) value="$(ovd_config_get_or "${file}" ovd.artifacts.policy require)" ;;
        *) ovd_error "Unknown OVD config field '${field}'."; return 1 ;;
    esac
    printf '%s\n' "${value}"
}

ovd_validate_config() {
    local name="$1" file="${2:-$(ovd_path "$1")/config.ini}" arch ram disk storage display network image
    [ -f "${file}" ] || ovd_error "Missing configuration: ${file}"
    name="$(ovd_config_value "${file}" name)"; [ "${name}" = "$1" ] || ovd_error "Config name mismatch."
    arch="$(ovd_config_value "${file}" arch)"; ovd_validate_arch "${arch}"
    ram="$(ovd_config_value "${file}" ram)"; ovd_validate_uint "RAM" "${ram}" 128 1048576
    disk="$(ovd_config_value "${file}" disk)"; ovd_validate_uint "Disk size" "${disk}" 1 16777216
    storage="$(ovd_config_value "${file}" storage)"; ovd_validate_profile "${storage}"
    ovd_profile_supported "${arch}" "${storage}" || ovd_error "Storage profile '${storage}' is not supported on ${arch}."
    display="$(ovd_config_value "${file}" display)"
    [ -z "${display}" ] || ovd_validate_display "${display}"
    network="$(ovd_config_value "${file}" network)"; ovd_validate_network "${network}"
    image="$(ovd_config_value "${file}" image)"
    [[ "${image}" != /* && "${image}" != *..* ]] || ovd_error "Storage image must remain inside the OVD directory."
    local profile_id filesystem boot_filesystem
    profile_id="$(ovd_config_value "${file}" profile_id)"
    filesystem="$(ovd_config_value "${file}" filesystem)"
    boot_filesystem="$(ovd_config_value "${file}" boot_filesystem)"
    if [ -n "${profile_id}" ]; then
        [ "${filesystem}" = ext4 ] || ovd_error "Profile-backed Omega system images must use ext4."
        [ -z "${boot_filesystem}" ] || [ "${boot_filesystem}" = fat32 ] || ovd_error "Profile boot filesystem must be fat32 or ext4."
        local artifact_policy
        artifact_policy="$(ovd_config_value "${file}" artifact_policy)"
        case "${artifact_policy}" in require|build-if-missing|build-if-stale|always-build|reuse-verified) ;; *) ovd_error "Unsupported artifact policy '${artifact_policy}'." ;; esac
    fi
    if [ "${OVD_SKIP_IMAGE_CHECK:-false}" != true ]; then
        [ -f "$(ovd_path "$1")/${image}" ] || ovd_error "Storage image is missing: ${image}"
    fi
}

ovd_state_dir() { printf '%s/state\n' "$(ovd_path "$1")"; }
ovd_log_file() { printf '%s/qemu.log\n' "$(ovd_state_dir "$1")"; }
ovd_pid_file() { printf '%s/qemu.pid\n' "$(ovd_state_dir "$1")"; }
ovd_command_file() { printf '%s/command.argv\n' "$(ovd_state_dir "$1")"; }
ovd_qmp_socket() { printf '%s/qmp.sock\n' "$(ovd_state_dir "$1")"; }

ovd_pid_running() {
    local pid_file
    pid_file="$(ovd_pid_file "$1")"
    [ -s "${pid_file}" ] || return 1
    local pid="$(cat "${pid_file}")"
    kill -0 "${pid}" 2>/dev/null
}

ovd_require_command() {
    command -v "$1" >/dev/null 2>&1 || ovd_error "Required command not found: $1"
}
