#!/usr/bin/env bash
# Build and validate the static Omega POSIX command suite for every ISA.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMMANDS=(ls dir ln pwd cat mkdir rm rmdir mv echo true false env test)

for arch in x86_64 aarch64 riscv64; do
    bash "${PROJECT_ROOT}/scripts/build_commands.sh" "${arch}"
    stage="${PROJECT_ROOT}/userland/commands/${arch}"
    [[ -f "${stage}/manifest.json" ]]
    for command in "${COMMANDS[@]}"; do
        artifact="${stage}/bin/${command}"
        [[ -x "${artifact}" ]] || { echo "missing command: ${artifact}" >&2; exit 1; }
        description="$(file "${artifact}")"
        [[ "${description}" == *"statically linked"* ]] || {
            echo "${artifact} is not static: ${description}" >&2
            exit 1
        }
        case "${arch}" in
            x86_64) [[ "${description}" == *"x86-64"* ]] ;;
            aarch64) [[ "${description}" == *"ARM aarch64"* ]] ;;
            riscv64) [[ "${description}" == *"RISC-V"* ]] ;;
        esac
    done
    echo "[commands] validated ${arch} (${#COMMANDS[@]} binaries)"
done
