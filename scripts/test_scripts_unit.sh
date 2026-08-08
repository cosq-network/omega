#!/usr/bin/env bash

# Unit tests for repository shell-script contracts.
# These tests do not boot QEMU or modify disk images. They validate syntax,
# accepted/rejected arguments, and the dry-run command generation path.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_QEMU="${PROJECT_ROOT}/scripts/run_qemu.sh"
OVD_RUN="${PROJECT_ROOT}/emulator/ovd_run.sh"
OVD_MANAGER="${PROJECT_ROOT}/emulator/ovd_manager.sh"

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/omega-script-tests.XXXXXX")"
cleanup() { rm -rf "${TMP_DIR}"; }
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
echo " Omega Shell Script Unit Tests"
echo "==============================================="

for script in \
    "${PROJECT_ROOT}/scripts/run_qemu.sh" \
    "${PROJECT_ROOT}/scripts/test.sh" \
    "${PROJECT_ROOT}/scripts/test_display.sh" \
    "${PROJECT_ROOT}/scripts/test_display_aarch64.sh" \
    "${PROJECT_ROOT}/scripts/test_storage.sh" \
    "${PROJECT_ROOT}/scripts/test_storage_unit.sh" \
    "${PROJECT_ROOT}/scripts/test_ext4_unit.sh" \
    "${PROJECT_ROOT}/scripts/create_bootable_disk.sh" \
    "${PROJECT_ROOT}/scripts/test_disk_images.sh" \
    "${PROJECT_ROOT}/emulator/libovd.sh" \
    "${OVD_MANAGER}" \
    "${OVD_RUN}" \
    "${PROJECT_ROOT}/emulator/test_ovd.sh" \
    "${PROJECT_ROOT}/emulator/test_ovd_unit.sh"; do
    bash -n "${script}" || fail "bash syntax: ${script}"
done
bash -n "${PROJECT_ROOT}/emulator/test_profile_ext4_integration.sh" || fail "bash syntax: profile ext4 integration test"
pass "shell syntax for scripts and emulator launchers"

expect_failure "run_qemu rejects unknown options" \
    bash "${RUN_QEMU}" --unknown-option
expect_failure "run_qemu rejects unknown storage profile" \
    bash "${RUN_QEMU}" --storage invalid --dry-run

for profile in auto virtio ahci usb none; do
    output="${TMP_DIR}/run-qemu-${profile}.log"
    bash "${RUN_QEMU}" --storage "${profile}" --dry-run >"${output}" 2>&1
    expect_contains "run_qemu dry-run emits a command (${profile})" "QEMU command:" "${output}"
done
pass "run_qemu accepts all x86_64 launcher storage profiles"

for option in \
    "--network user" \
    "--network socket" \
    "--ephemeral" \
    "--readonly" \
    "--qmp" \
    "--no-build"; do
    output="${TMP_DIR}/run-qemu-option-${option// /-}.log"
    # --dry-run keeps this contract test independent of QEMU execution.
    # shellcheck disable=SC2086
    bash "${RUN_QEMU}" ${option} --dry-run >"${output}" 2>&1
    expect_contains "run_qemu accepts ${option}" "QEMU command:" "${output}"
done

# The storage unit test is itself a script-level contract and must remain
# runnable independently from the full architecture/QEMU suite.
bash "${PROJECT_ROOT}/scripts/test_storage_unit.sh" >"${TMP_DIR}/storage-unit.log" 2>&1
expect_contains "standalone storage unit runner passes" "[PASS] Storage host unit tests" "${TMP_DIR}/storage-unit.log"

echo "[PASS] Shell script unit tests completed"
