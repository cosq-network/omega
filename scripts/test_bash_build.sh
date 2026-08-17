#!/usr/bin/env bash
# Build and validate the non-interactive Bash profile for every Omega ISA.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for arch in x86_64 aarch64 riscv64; do
    bash "${PROJECT_ROOT}/scripts/build_bash.sh" "${arch}"
    artifact="${PROJECT_ROOT}/userland/bash/${arch}/bin/bash"
    manifest="${PROJECT_ROOT}/userland/bash/${arch}/manifest.json"
    test -s "${artifact}"
    test -s "${manifest}"
    file_output="$(file "${artifact}")"
    case "${arch}" in
        x86_64) [[ "${file_output}" == *"x86-64"* ]] ;;
        aarch64) [[ "${file_output}" == *"ARM aarch64"* ]] ;;
        riscv64) [[ "${file_output}" == *"RISC-V"* ]] ;;
    esac
    [[ "${file_output}" == *"statically linked"* ]]
    grep -Fq '"interactive": false' "${manifest}"
    grep -Fq '"readline": false' "${manifest}"
    grep -Fq '"job_control": false' "${manifest}"
    echo "[bash][PASS] ${arch} static non-interactive Bash artifact"
done
