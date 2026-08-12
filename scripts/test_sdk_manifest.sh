#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_ROOT="$(mktemp -d /tmp/omega-sdk-manifest.XXXXXX)"
trap 'rm -rf "${TEST_ROOT}"' EXIT

printf 'Omega SDK manifest test\n' >"${TEST_ROOT}/app"
python3 "${PROJECT_ROOT}/scripts/sdk_manifest.py" create \
    --root "${TEST_ROOT}" --output "${TEST_ROOT}/manifest.json" \
    --name omega-test --version 0.1.0 --target x86_64-omega \
    --profile omega-c --entry app --artifact app --permission stdout
python3 "${PROJECT_ROOT}/scripts/sdk_manifest.py" validate \
    "${TEST_ROOT}/manifest.json" --root "${TEST_ROOT}"
printf 'tamper\n' >>"${TEST_ROOT}/app"
if python3 "${PROJECT_ROOT}/scripts/sdk_manifest.py" validate \
    "${TEST_ROOT}/manifest.json" --root "${TEST_ROOT}" >/dev/null 2>&1; then
    echo "manifest tamper was not rejected" >&2
    exit 1
fi
echo "[SUCCESS] Omega SDK manifest creation and tamper validation passed."
