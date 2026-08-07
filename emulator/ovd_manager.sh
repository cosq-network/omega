#!/usr/bin/env bash

set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=libovd.sh
source "$(dirname "${BASH_SOURCE[0]}")/libovd.sh"

GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'
mkdir -p "${OVD_ROOT}"

usage() {
    cat <<USAGE
Usage:
  $0 create --name NAME --arch ARCH [--ram MB] [--disk MB] [--storage PROFILE]
            [--network none|user|socket] [--display PROFILE] [--initrd FILE]
  $0 list [--json]
  $0 show|validate|status|logs --name NAME
  $0 start --name NAME [launcher options]
  $0 stop --name NAME [--force]
  $0 snapshot create|list|apply --name NAME [--snapshot NAME]
  $0 clone --name SOURCE --new-name NAME
  $0 export --name NAME --output ARCHIVE
  $0 import --archive ARCHIVE --name NAME
  $0 delete --name NAME [--force]

Architectures: x86_64, aarch64, riscv64
Storage: auto, virtio, ahci, usb, sd, optical, none
Display: standard-vga, simplefb, virtio-gpu, none
USAGE
    exit 1
}

require_name() { [ -n "${1:-}" ] || { echo -e "${RED}[ERROR] OVD name is required.${NC}" >&2; usage; }; ovd_validate_name "$1"; }

create_ovd() {
    local name='' arch='x86_64' ram=1024 disk=64 storage=virtio network=none display='' initrd=''
    while (($#)); do
        case "$1" in
            --name) name="${2:-}"; shift 2;; --arch) arch="${2:-}"; shift 2;;
            --ram) ram="${2:-}"; shift 2;; --disk) disk="${2:-}"; shift 2;;
            --storage) storage="${2:-}"; shift 2;; --network) network="${2:-}"; shift 2;;
            --display) display="${2:-}"; shift 2;; --initrd) initrd="${2:-}"; shift 2;;
            *) usage;;
        esac
    done
    require_name "${name}"; ovd_validate_arch "${arch}"
    ovd_validate_uint RAM "${ram}" 128 1048576; ovd_validate_uint Disk "${disk}" 1 16777216
    ovd_validate_profile "${storage}"; ovd_profile_supported "${arch}" "${storage}" || ovd_error "${storage} is unavailable on ${arch}."
    ovd_validate_network "${network}"
    if [ -z "${display}" ]; then display="$([ "${arch}" = x86_64 ] && echo standard-vga || echo simplefb)"; fi
    ovd_validate_display "${display}"
    [ -z "${initrd}" ] || [ -f "${initrd}" ] || ovd_error "Initrd not found: ${initrd}"
    local path; path="$(ovd_path "${name}")"
    [ ! -e "${path}" ] || ovd_error "OVD '${name}' already exists."
    mkdir -p "${path}/state"
    cat > "${path}/config.ini" <<EOF
ovd.schema=${OVD_SCHEMA_VERSION}
ovd.name=${name}
ovd.arch=${arch}
ovd.ram_mb=${ram}
ovd.disk_mb=${disk}
ovd.storage.profile=${storage}
ovd.storage.image=userdata.img
ovd.storage.format=raw
ovd.storage.readonly=false
ovd.display.profile=${display}
ovd.network.profile=${network}
ovd.initrd=${initrd}
EOF
    dd if=/dev/zero of="${path}/userdata.img" bs=1M count="${disk}" status=none
    echo -e "${GREEN}[+] Created OVD '${name}' (${arch}, ${ram} MB RAM, ${disk} MB disk).${NC}"
}

list_ovd() {
    local json=false; [ "${1:-}" = --json ] && json=true
    if $json; then printf '['; local first=true; else echo "Omega Virtual Devices:"; fi
    shopt -s nullglob
    local path name file arch ram disk storage state
    for path in "${OVD_ROOT}"/*; do
        [ -f "${path}/config.ini" ] || continue
        name="$(basename "${path}")"; file="${path}/config.ini"; arch="$(ovd_config_value "${file}" arch)"; ram="$(ovd_config_value "${file}" ram)"; disk="$(ovd_config_value "${file}" disk)"; storage="$(ovd_config_value "${file}" storage)"; state=stopped; ovd_pid_running "${name}" && state=running || true
        if $json; then
            $first || printf ','; first=false
            printf '{"name":"%s","arch":"%s","ram_mb":%s,"disk_mb":%s,"storage":"%s","state":"%s"}' "$name" "$arch" "$ram" "$disk" "$storage" "$state"
        else
            printf 'Device: %s\n' "$name"
            printf '  ovd.arch=%s\n  ovd.ram=%s\n  ovd.disk=%s\n  ovd.storage=%s\n  ovd.state=%s\n' "$arch" "$ram" "$disk" "$storage" "$state"
            echo "-----------------------------------------"
        fi
    done
    if $json; then echo ']'; fi
}

show_ovd() { local name="${1:-}"; require_name "${name}"; local path; path="$(ovd_path "${name}")"; ovd_validate_config "${name}"; cat "${path}/config.ini"; echo "state=$([ "$(ovd_pid_running "${name}" && echo running || echo stopped)" = running ] && echo running || echo stopped)"; }
validate_ovd() { local name="${1:-}"; require_name "${name}"; ovd_validate_config "${name}"; echo "[PASS] OVD '${name}' configuration is valid."; }
start_ovd() { local name=""; local args=(); while (($#)); do case "$1" in --name) name="${2:-}"; shift 2;; *) args+=("$1"); shift;; esac; done; require_name "${name}"; exec "${PROJECT_ROOT}/emulator/ovd_run.sh" run --name "${name}" "${args[@]}"; }
stop_ovd() { local name="" force=false; while (($#)); do case "$1" in --name) name="${2:-}"; shift 2;; --force) force=true; shift;; *) usage;; esac; done; require_name "${name}"; local pid_file qmp pid; pid_file="$(ovd_pid_file "${name}")"; qmp="$(ovd_qmp_socket "${name}")"; if ! ovd_pid_running "${name}"; then rm -f "${pid_file}" "${qmp}"; echo "OVD '${name}' is not running."; return 0; fi; pid="$(cat "${pid_file}")"; if [ -S "${qmp}" ] && command -v nc >/dev/null 2>&1; then printf '%s\n' '{"execute":"qmp_capabilities"}' '{"execute":"quit"}' | nc -U "${qmp}" >/dev/null 2>&1 || true; fi; kill -TERM "${pid}" 2>/dev/null || true; for _ in {1..20}; do ovd_pid_running "${name}" || break; sleep .1; done; if ovd_pid_running "${name}" && $force; then kill -KILL "${pid}" 2>/dev/null || true; fi; rm -f "${pid_file}" "${qmp}"; echo "[+] Stopped '${name}'."; }
delete_ovd() { local name='' force=false; while (($#)); do case "$1" in --name) name="${2:-}"; shift 2;; --force) force=true; shift;; *) usage;; esac; done; require_name "${name}"; local path; path="$(ovd_path "${name}")"; [ -d "${path}" ] || ovd_error "OVD '${name}' not found."; ovd_pid_running "${name}" && { $force && stop_ovd --name "${name}" --force || ovd_error "OVD is running; stop it or use --force."; }; rm -rf -- "${path}"; echo -e "${GREEN}[+] Deleted '${name}'.${NC}"; }
clone_ovd() { local source='' new=''; while (($#)); do case "$1" in --name) source="${2:-}"; shift 2;; --new-name) new="${2:-}"; shift 2;; *) usage;; esac; done; require_name "${source}"; require_name "${new}"; local src dst image; src="$(ovd_path "${source}")"; dst="$(ovd_path "${new}")"; [ -d "${src}" ] || ovd_error "Source OVD not found."; [ ! -e "${dst}" ] || ovd_error "Destination OVD already exists."; mkdir -p "${dst}/state"; cp "${src}/config.ini" "${dst}/config.ini"; image="$(ovd_config_value "${src}/config.ini" image)"; cp "${src}/${image}" "${dst}/${image}"; sed -i.bak "s/^ovd.name=.*/ovd.name=${new}/" "${dst}/config.ini"; rm -f "${dst}/config.ini.bak"; echo "[+] Cloned '${source}' to '${new}' without runtime state."; }
archive_ovd() { local name='' output=''; while (($#)); do case "$1" in --name) name="${2:-}"; shift 2;; --output) output="${2:-}"; shift 2;; *) usage;; esac; done; require_name "${name}"; [ -n "${output}" ] || usage; ovd_validate_config "${name}"; tar -czf "${output}" -C "${OVD_ROOT}" "${name}"; echo "[+] Exported '${name}' to ${output}."; }
import_ovd() { local archive='' name=''; while (($#)); do case "$1" in --archive) archive="${2:-}"; shift 2;; --name) name="${2:-}"; shift 2;; *) usage;; esac; done; require_name "${name}"; [ -f "${archive}" ] || ovd_error "Archive not found."; local tmp source entry root; tmp="$(mktemp -d "${TMPDIR:-/tmp}/omega-ovd-import.XXXXXX")"; trap 'rm -rf "${tmp}"' RETURN; root=''; while IFS= read -r entry; do entry="${entry%/}"; [ -z "${entry}" ] && continue; case "${entry}" in /*|../*|*/../*|*/..|*"$'\\n'"*) ovd_error "Archive contains unsafe path: ${entry}";; esac; if [ -z "${root}" ]; then root="${entry%%/*}"; ovd_validate_name "${root}"; elif [ "${entry%%/*}" != "${root}" ]; then ovd_error "Archive must contain one OVD root."; fi; done < <(tar -tzf "${archive}") || ovd_error "Invalid or unsafe OVD archive."; tar -xzf "${archive}" -C "${tmp}"; source="${tmp}/${root}"; [ -d "${source}" ] || ovd_error "Archive has no OVD directory."; [ ! -e "$(ovd_path "${name}")" ] || ovd_error "Destination already exists."; mv "${source}" "$(ovd_path "${name}")"; sed -i.bak "s/^ovd.name=.*/ovd.name=${name}/" "$(ovd_path "${name}")/config.ini"; rm -f "$(ovd_path "${name}")/config.ini.bak"; validate_ovd "${name}"; echo "[+] Imported '${name}'."; }
snapshot_ovd() { local action="${1:-}" name='' snap=''; shift || true; while (($#)); do case "$1" in --name) name="${2:-}"; shift 2;; --snapshot) snap="${2:-}"; shift 2;; *) usage;; esac; done; require_name "${name}"; ovd_validate_config "${name}"; local image; image="$(ovd_path "${name}")/$(ovd_config_value "$(ovd_path "${name}")/config.ini" image)"; ovd_require_command qemu-img; case "${action}" in create) [ -n "${snap}" ] || ovd_error "Snapshot name is required."; qemu-img snapshot -c "${snap}" "${image}";; list) qemu-img snapshot -l "${image}";; apply) [ -n "${snap}" ] || ovd_error "Snapshot name is required."; qemu-img snapshot -a "${snap}" "${image}";; *) usage;; esac; }

command="${1:-}"; shift || true
case "${command}" in
    create) create_ovd "$@";; list) list_ovd "$@";; show) [[ "${1:-}" = --name ]] || usage; show_ovd "${2:-}";; validate) [[ "${1:-}" = --name ]] || usage; validate_ovd "${2:-}";; start) start_ovd "$@";; stop) stop_ovd "$@";; status) [[ "${1:-}" = --name ]] || usage; ovd_validate_config "${2:-}"; ovd_pid_running "${2:-}" && echo running || echo stopped;; logs) [[ "${1:-}" = --name ]] || usage; cat "$(ovd_log_file "${2:-}")" 2>/dev/null || ovd_error "No QEMU log exists.";; delete) delete_ovd "$@";; clone) clone_ovd "$@";; export) archive_ovd "$@";; import) import_ovd "$@";; snapshot) snapshot_ovd "$@";; *) usage;;
esac
