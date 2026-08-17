#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for arch in x86_64 aarch64 riscv64; do
    bash "${PROJECT_ROOT}/scripts/build_musl_sysroot.sh" "${arch}" >/dev/null
    sdk="${PROJECT_ROOT}/libc/omega-sdk/${arch}"
    test -f "${sdk}/lib/libc.a"
    test -f "${sdk}/lib/crt1.o"
    test -f "${sdk}/lib/libomega-shim.a"
    test -f "${sdk}/include/bits/errno.h"
    python3 - "${sdk}/omega-sdk.json" "${arch}" <<'PY'
import json
import sys

manifest = json.load(open(sys.argv[1], encoding="utf-8"))
assert manifest["name"] == "omega-posix-static"
assert manifest["architecture"] == sys.argv[2]
assert manifest["musl_version"] == "1.2.5"
assert manifest["static"] is True
PY
    echo "[PASS] musl ${arch} sysroot"
done

for arch in x86_64 aarch64 riscv64; do
    bash "${PROJECT_ROOT}/scripts/build_tcc.sh" "${arch}" >/dev/null
    test -x "${PROJECT_ROOT}/build/tcc-${arch}/tcc"
    file "${PROJECT_ROOT}/build/tcc-${arch}/tcc" | grep -Fq "ELF 64-bit"
    if [[ "${arch}" != x86_64 ]]; then
        test -s "${PROJECT_ROOT}/libc/omega-sdk/${arch}/lib/libtcc1.a"
    fi
    echo "[PASS] TinyCC ${arch} static Omega ELF"
done

echo "[SUCCESS] musl sysroots and TinyCC integration passed for all ISAs."
