#!/usr/bin/env bash
# Build the standalone Omega POSIX command suite for one ISA.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${1:-x86_64}"
SDK="${PROJECT_ROOT}/libc/omega-sdk/${ARCH}"
BUILD="${PROJECT_ROOT}/build/commands-${ARCH}"
STAGE="${PROJECT_ROOT}/userland/commands/${ARCH}"

case "${ARCH}" in
    x86_64) TARGET=x86_64-unknown-none-elf; EXTRA=(-mno-red-zone -mcmodel=large) ;;
    aarch64) TARGET=aarch64-unknown-none-elf; EXTRA=() ;;
    riscv64) TARGET=riscv64-unknown-none-elf; EXTRA=(-march=rv64gc -mabi=lp64d -mcmodel=medany) ;;
    *) echo "unsupported architecture: ${ARCH}" >&2; exit 2 ;;
esac

CLANG_BIN="${CLANG_BIN:-clang}"
if [[ "${ARCH}" == "riscv64" && "${CLANG_BIN}" == clang && -x /opt/homebrew/opt/llvm/bin/clang ]]; then
    CLANG_BIN=/opt/homebrew/opt/llvm/bin/clang
fi
AR_BIN="${AR_BIN:-$(command -v llvm-ar || command -v ar)}"
LD_BIN="${LD_BIN:-$(command -v ld.lld || true)}"
[[ -n "${LD_BIN}" ]] || { echo "ld.lld is required" >&2; exit 1; }
[[ -f "${SDK}/lib/libc.a" ]] || bash "${PROJECT_ROOT}/scripts/build_musl_sysroot.sh" "${ARCH}"

RUNTIME_LIB=""
[[ -f "${SDK}/lib/libcompiler_rt.a" ]] && RUNTIME_LIB="${SDK}/lib/libcompiler_rt.a"
[[ -z "${RUNTIME_LIB}" && -f "${SDK}/lib/libtcc1.a" ]] && RUNTIME_LIB="${SDK}/lib/libtcc1.a"
if [[ -z "${RUNTIME_LIB}" && "${ARCH}" != x86_64 ]]; then
    bash "${PROJECT_ROOT}/scripts/build_tcc.sh" "${ARCH}"
    RUNTIME_LIB="${SDK}/lib/libtcc1.a"
fi

rm -rf "${BUILD}"
mkdir -p "${BUILD}/obj" "${STAGE}/bin"
COMMON_FLAGS=(--target="${TARGET}" -std=c11 -D_GNU_SOURCE -O2 -g -ffreestanding
    -fno-stack-protector -fno-builtin -nostdinc
    -isystem "${SDK}/include" -I"${PROJECT_ROOT}/userland/commands/include")
if ((${#EXTRA[@]})); then
    COMMON_FLAGS+=("${EXTRA[@]}")
fi

compile() {
    local source="$1" output="$2"; shift 2
    "${CLANG_BIN}" "${COMMON_FLAGS[@]}" "$@" -c "${PROJECT_ROOT}/userland/commands/${source}" -o "${BUILD}/obj/${output}"
}

compile common/common.c common.o
compile ls.c ls.o -DOMEGA_LS_NO_MAIN
compile ls.c ls-main.o
compile dir.c dir.o
compile cat.c cat.o
compile pwd.c pwd.o
compile mkdir.c mkdir.o
compile rm.c rm.o
compile rmdir.c rmdir.o
compile mv.c mv.o
compile ln.c ln.o
compile echo.c echo.o
compile status.c true.o
compile fail.c false.o
compile env.c env.o
compile test.c test.o

link() {
    local name="$1"; shift
    "${LD_BIN}" -flavor gnu -T "${SDK}/lib/omega.ld" -o "${BUILD}/obj/${name}" \
        "${SDK}/lib/crt1.o" "$@" "${SDK}/lib/libc.a" \
        "${SDK}/lib/libomega-shim.a" ${RUNTIME_LIB}
    cp "${BUILD}/obj/${name}" "${STAGE}/bin/${name}"
}

COMMON_OBJ="${BUILD}/obj/common.o"
link ls "${BUILD}/obj/ls-main.o" "${COMMON_OBJ}"
link dir "${BUILD}/obj/dir.o" "${BUILD}/obj/ls.o" "${COMMON_OBJ}"
link cat "${BUILD}/obj/cat.o" "${COMMON_OBJ}"
link pwd "${BUILD}/obj/pwd.o"
link mkdir "${BUILD}/obj/mkdir.o" "${COMMON_OBJ}"
link rm "${BUILD}/obj/rm.o" "${COMMON_OBJ}"
link rmdir "${BUILD}/obj/rmdir.o" "${COMMON_OBJ}"
link mv "${BUILD}/obj/mv.o" "${COMMON_OBJ}"
link ln "${BUILD}/obj/ln.o" "${COMMON_OBJ}"
link echo "${BUILD}/obj/echo.o"
link true "${BUILD}/obj/true.o"
link false "${BUILD}/obj/false.o"
link env "${BUILD}/obj/env.o" "${COMMON_OBJ}"
link test "${BUILD}/obj/test.o"

cat > "${STAGE}/manifest.json" <<EOF
{
  "name": "omega-posix-commands",
  "architecture": "${ARCH}",
  "static": true,
  "commands": ["ls", "dir", "ln", "pwd", "cat", "mkdir", "rm", "rmdir", "mv", "echo", "true", "false", "env", "test"]
}
EOF
echo "[commands] Omega ${ARCH} command suite: ${STAGE}/bin"
