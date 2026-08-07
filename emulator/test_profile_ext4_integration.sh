#!/usr/bin/env bash

# Profile-backed artifact and launcher integration test. The real ext4 path is
# exercised when mke2fs/mkfs.ext4 is installed; dry-run validation remains
# useful on hosts that intentionally do not carry filesystem creation tools.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROFILE_TOOL="${PROJECT_ROOT}/emulator/profile_catalog.py"
MANAGER="${PROJECT_ROOT}/emulator/ovd_manager.sh"
RUNNER="${PROJECT_ROOT}/emulator/ovd_run.sh"
TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/omega-profile-ext4.XXXXXX")"
trap 'rm -rf "${TMP_ROOT}"' EXIT

pass() { echo "[PASS] $*"; }
fail() { echo "[FAIL] $*" >&2; exit 1; }

export OMEGA_PROJECT_ROOT="${PROJECT_ROOT}"
export OMEGA_BUILD_ROOT="${TMP_ROOT}/build"
export OMEGA_IMAGE_ROOT="${TMP_ROOT}/images"
export OMEGA_OVD_ROOT="${TMP_ROOT}/ovd"

python3 "${PROFILE_TOOL}" validate --profile x86_64-desktop-q35 >/dev/null || fail "x86_64 profile validation"
dry_run="${TMP_ROOT}/artifacts.json"
python3 "${PROFILE_TOOL}" artifacts --profile x86_64-desktop-q35 --dry-run >"${dry_run}"
python3 - "${dry_run}" <<'PY' || exit 1
import json
import sys

value = json.load(open(sys.argv[1], encoding="utf-8"))
assert value["filesystem"] == "ext4"
assert value["disk"]["filesystem"] == "ext4"
assert value["disk"]["status"] == "stale"
assert "mke2fs or mkfs.ext4" in value["disk"]["required_tools"]
PY
pass "profile artifact dry-run requires ext4 and the current kernel"

MKFS="$(command -v mke2fs || command -v mkfs.ext4 || true)"
if [ -z "${MKFS}" ]; then
    echo "[SKIP] profile-backed ext4 image integration requires mke2fs or mkfs.ext4"
    exit 0
fi

python3 "${PROFILE_TOOL}" artifacts --profile x86_64-desktop-q35 >/dev/null
image="${OMEGA_IMAGE_ROOT}/omega-x86_64-desktop-q35-ext4.raw"
[ -s "${image}" ] || fail "profile ext4 image was not created"
manifest="${image%.raw}.artifact.json"
python3 - "${manifest}" "${image}" <<'PY' || exit 1
import json
import os
import sys

manifest = json.load(open(sys.argv[1], encoding="utf-8"))
assert manifest["filesystem"] == "ext4"
assert manifest["kind"] == "omega-system-image"
assert os.path.getsize(sys.argv[2]) == manifest["size_mb"] * 1024 * 1024
PY
pass "profile image is a manifest-backed ext4 raw image"

"${MANAGER}" create-from-profile --profile x86_64-desktop-q35 --name ext4-profile-test >/dev/null
config="${OMEGA_OVD_ROOT}/ext4-profile-test/config.ini"
local_image="${OMEGA_OVD_ROOT}/ext4-profile-test/system.ext4"
grep -Fq 'ovd.profile.id=x86_64-desktop-q35' "${config}" || fail "OVD profile identity"
grep -Fq 'ovd.filesystem.system=ext4' "${config}" || fail "OVD ext4 filesystem declaration"
cmp -s "${image}" "${local_image}" || fail "OVD image copied from profile artifact"
pass "profile-backed OVD contains the current ext4 artifact"

"${RUNNER}" run --name ext4-profile-test --dry-run >"${TMP_ROOT}/run.log"
grep -Fq 'virtio-blk-pci,disable-modern=on' "${TMP_ROOT}/run.log" || fail "x86_64 profile uses transitional VirtIO-Block"
grep -Fq 'system.ext4' "${TMP_ROOT}/run.log" || fail "launcher uses the profile ext4 image"
pass "profile-backed OVD dry-run wires kernel, ext4 image, and VirtIO-Block"
