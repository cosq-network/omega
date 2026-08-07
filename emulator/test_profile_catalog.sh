#!/usr/bin/env bash

set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOL="${PROJECT_ROOT}/emulator/profile_catalog.py"
MANAGER="${PROJECT_ROOT}/emulator/ovd_manager.sh"
TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/omega-profile-tests.XXXXXX")"
trap 'rm -rf "${TMP_ROOT}"' EXIT

pass() { echo "[PASS] $*"; }
fail() { echo "[FAIL] $*" >&2; exit 1; }

python3 -m py_compile "${TOOL}" || fail "profile catalog compiles"
python3 "${TOOL}" validate --json | python3 -c 'import json,sys; value=json.load(sys.stdin); assert value["valid"] and value["profiles"] >= 3' || fail "catalog validates"
python3 "${TOOL}" list --json | python3 -c 'import json,sys; value=json.load(sys.stdin); ids={item["profile_id"] for item in value}; assert "x86_64-desktop-q35" in ids and "aarch64-virt-development" in ids and "riscv64-virt-development" in ids' || fail "catalog lists all native architectures"
python3 "${TOOL}" list --tsv | python3 -c 'import sys; rows=[line.rstrip("\n").split("\t") for line in sys.stdin if line.strip()]; assert all(len(row) == 8 for row in rows); native=next(row for row in rows if row[0] == "riscv64-virt-minimal"); assert native[5] == "512" and native[6] == "64" and native[7] == "true"' || fail "TSV exposes catalog defaults and native capability"
python3 "${TOOL}" show --profile x86_64-desktop-q35 --json | python3 -c 'import json,sys; value=json.load(sys.stdin); assert value["filesystem"]["system"] == "ext4" and value["artifacts"]["disk"]["filesystem"] == "ext4"' || fail "native profiles use ext4"
python3 "${TOOL}" render --profile riscv64-virt-minimal | python3 -c 'import json,sys; value=json.load(sys.stdin); assert value["arguments"][0] == "qemu-system-riscv64" and "-M" in value["arguments"] and "virt" in value["arguments"]' || fail "rendering is deterministic and architecture-specific"
OMEGA_BUILD_ROOT="${TMP_ROOT}/build" OMEGA_IMAGE_ROOT="${TMP_ROOT}/images" python3 "${TOOL}" artifacts --profile aarch64-virt-development --dry-run >"${TMP_ROOT}/artifacts.json"
python3 -c 'import json,sys; value=json.load(open(sys.argv[1])); assert value["filesystem"] == "ext4" and value["disk"]["status"] == "stale" and "mke2fs or mkfs.ext4" in value["disk"]["required_tools"]' "${TMP_ROOT}/artifacts.json" || fail "dry-run reports missing ext4 artifact"
"${MANAGER}" profiles validate --profile x86_64-desktop-q35 --json >/dev/null || fail "manager delegates profile validation"
pass "OVD profile catalog tests"
