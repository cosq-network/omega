#!/usr/bin/env bash

# Unit tests for OVD configuration and QEMU command construction.
# Unlike test_ovd.sh, this suite never starts QEMU; all launches use --dry-run.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OVD_MANAGER="${PROJECT_ROOT}/emulator/ovd_manager.sh"
OVD_RUN="${PROJECT_ROOT}/emulator/ovd_run.sh"
OVD_ROOT="${PROJECT_ROOT}/emulator/ovd"
NAME="unit_ovd_${$}"
CLONE_NAME="${NAME}_clone"
IMPORT_NAME="${NAME}_import"
OVD_PATH="${OVD_ROOT}/${NAME}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/omega-ovd-unit.XXXXXX")"

cleanup() {
    bash "${OVD_MANAGER}" stop --name "${NAME}" --force > /dev/null 2>&1 || true
    for cleanup_name in "${NAME}" "${CLONE_NAME}" "${IMPORT_NAME}"; do
        bash "${OVD_MANAGER}" stop --name "${cleanup_name}" --force > /dev/null 2>&1 || true
        bash "${OVD_MANAGER}" delete --name "${cleanup_name}" --force > /dev/null 2>&1 || true
    done
    rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1" >&2; exit 1; }

expect_failure() {
    local label="$1"
    shift
    if "$@" >"${TMP_DIR}/failure.log" 2>&1; then
        fail "${label} unexpectedly succeeded"
    fi
    pass "${label}"
}

expect_contains() {
    local label="$1"
    local needle="$2"
    local file="$3"
    grep -Fq -- "${needle}" "${file}" || {
        echo "--- ${file} ---" >&2
        cat "${file}" >&2
        fail "${label}"
    }
    pass "${label}"
}

echo "==============================================="
echo " Omega OVD Unit Tests"
echo "==============================================="

bash -n "${OVD_MANAGER}" "${OVD_RUN}" || fail "OVD shell syntax"
pass "OVD shell syntax"

expect_failure "manager rejects missing name" \
    bash "${OVD_MANAGER}" create --arch x86_64
expect_failure "manager rejects invalid storage profile" \
    bash "${OVD_MANAGER}" create --name "${NAME}" --storage invalid
expect_failure "manager rejects path traversal names" \
    bash "${OVD_MANAGER}" create --name "../unsafe" --storage virtio
expect_failure "manager rejects invalid RAM values" \
    bash "${OVD_MANAGER}" create --name "${NAME}" --ram 1
expect_failure "manager rejects invalid disk values" \
    bash "${OVD_MANAGER}" create --name "${NAME}" --disk 0

bash "${OVD_MANAGER}" create \
    --name "${NAME}" --arch x86_64 --ram 256 --disk 8 --storage virtio >"${TMP_DIR}/create.log"

[ -f "${OVD_PATH}/config.ini" ] || fail "manager creates config.ini"
[ -f "${OVD_PATH}/userdata.img" ] || fail "manager creates userdata.img"
grep -Fxq "ovd.arch=x86_64" "${OVD_PATH}/config.ini" || fail "architecture persisted"
grep -Fxq "ovd.storage.profile=virtio" "${OVD_PATH}/config.ini" || fail "storage profile persisted"
grep -Fxq "ovd.storage.image=userdata.img" "${OVD_PATH}/config.ini" || fail "storage image persisted"
pass "manager persists OVD storage configuration"

bash "${OVD_MANAGER}" list >"${TMP_DIR}/list.log"
expect_contains "manager lists created OVD" "${NAME}" "${TMP_DIR}/list.log"
bash "${OVD_MANAGER}" list --json >"${TMP_DIR}/list.json"
expect_contains "manager emits machine-readable list" '"name":"' "${TMP_DIR}/list.json"

for profile in virtio ahci usb sd optical none; do
    output="${TMP_DIR}/profile-${profile}.log"
    bash "${OVD_RUN}" run --name "${NAME}" --no-gpu --storage "${profile}" --dry-run >"${output}" 2>&1
    expect_contains "dry-run succeeds for ${profile}" "QEMU command:" "${output}"
done

expect_contains "virtio profile selects PCI block device" "virtio-blk-pci" "${TMP_DIR}/profile-virtio.log"
expect_contains "ahci profile selects IDE disk transport" "if=ide" "${TMP_DIR}/profile-ahci.log"
expect_contains "usb profile selects USB mass storage" "usb-storage" "${TMP_DIR}/profile-usb.log"
expect_contains "sd profile selects SD transport" "if=sd" "${TMP_DIR}/profile-sd.log"
expect_contains "optical profile selects CD device" "ide-cd" "${TMP_DIR}/profile-optical.log"
if grep -Fq "userdata.img" "${TMP_DIR}/profile-none.log"; then
    fail "none profile attaches a storage image"
fi
pass "none profile omits storage attachment"

expect_failure "launcher rejects invalid storage profile" \
    bash "${OVD_RUN}" run --name "${NAME}" --storage invalid --dry-run
ARM_NAME="${NAME}_arm"
ARM_PATH="${OVD_ROOT}/${ARM_NAME}"
bash "${OVD_MANAGER}" create --name "${ARM_NAME}" --arch aarch64 --storage virtio > /dev/null
expect_failure "launcher rejects AHCI on AArch64" \
    bash "${OVD_RUN}" run --name "${ARM_NAME}" --storage ahci --dry-run
bash "${OVD_MANAGER}" delete --name "${ARM_NAME}" > /dev/null
[ ! -e "${ARM_PATH}" ] || fail "manager removes architecture-specific OVD state"

cat > "${TMP_DIR}/qemu-system-x86_64" <<'FAKEQEMU'
#!/usr/bin/env bash
echo "$*"
sleep 30
FAKEQEMU
chmod +x "${TMP_DIR}/qemu-system-x86_64"
PATH="${TMP_DIR}:${PATH}" bash "${OVD_RUN}" run --name "${NAME}" --no-gpu --daemon >"${TMP_DIR}/daemon.log" 2>&1
expect_contains "daemon launcher records a command" "Started '${NAME}'" "${TMP_DIR}/daemon.log"
expect_contains "manager reports running OVD" "running" <(PATH="${TMP_DIR}:${PATH}" bash "${OVD_MANAGER}" status --name "${NAME}")
PATH="${TMP_DIR}:${PATH}" bash "${OVD_MANAGER}" stop --name "${NAME}" >"${TMP_DIR}/stop.log"
expect_contains "manager stops daemon OVD" "Stopped '${NAME}'" "${TMP_DIR}/stop.log"

bash "${OVD_MANAGER}" clone --name "${NAME}" --new-name "${CLONE_NAME}" >"${TMP_DIR}/clone.log"
bash "${OVD_MANAGER}" validate --name "${CLONE_NAME}" >"${TMP_DIR}/clone-validate.log"
expect_contains "clone creates an independently valid OVD" "valid" "${TMP_DIR}/clone-validate.log"
bash "${OVD_MANAGER}" export --name "${NAME}" --output "${TMP_DIR}/ovd.tar.gz" >"${TMP_DIR}/export.log"
bash "${OVD_MANAGER}" delete --name "${CLONE_NAME}" --force > /dev/null
bash "${OVD_MANAGER}" import --archive "${TMP_DIR}/ovd.tar.gz" --name "${IMPORT_NAME}" >"${TMP_DIR}/import.log"
bash "${OVD_MANAGER}" validate --name "${IMPORT_NAME}" >"${TMP_DIR}/import-validate.log"
expect_contains "export/import preserves a valid OVD" "valid" "${TMP_DIR}/import-validate.log"

cat > "${TMP_DIR}/qemu-img" <<'FAKEQEMUIMG'
#!/usr/bin/env bash
echo "qemu-img $*"
FAKEQEMUIMG
chmod +x "${TMP_DIR}/qemu-img"
PATH="${TMP_DIR}:${PATH}" bash "${OVD_MANAGER}" snapshot create --name "${NAME}" --snapshot baseline >"${TMP_DIR}/snapshot-create.log"
expect_contains "snapshot create dispatches to qemu-img" "qemu-img snapshot -c baseline" "${TMP_DIR}/snapshot-create.log"
PATH="${TMP_DIR}:${PATH}" bash "${OVD_MANAGER}" snapshot list --name "${NAME}" >"${TMP_DIR}/snapshot-list.log"
expect_contains "snapshot list dispatches to qemu-img" "qemu-img snapshot -l" "${TMP_DIR}/snapshot-list.log"
PATH="${TMP_DIR}:${PATH}" bash "${OVD_MANAGER}" snapshot apply --name "${NAME}" --snapshot baseline >"${TMP_DIR}/snapshot-apply.log"
expect_contains "snapshot apply dispatches to qemu-img" "qemu-img snapshot -a baseline" "${TMP_DIR}/snapshot-apply.log"

bash "${OVD_MANAGER}" delete --name "${NAME}" >"${TMP_DIR}/delete.log"
[ ! -e "${OVD_PATH}" ] || fail "manager removes OVD directory"
pass "manager cleanup removes generated OVD state"

echo "[PASS] OVD unit tests completed"
