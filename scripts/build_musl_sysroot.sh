#!/usr/bin/env bash
# Build the Omega musl sysroot for a target ISA.
#
# Produces, for each ISA:
#   libc/omega-sdk/<isa>/
#     include/          # musl headers (C)
#     lib/libc.a        # static musl
#     lib/crt1.o        # musl startup
#     lib/libomega-shim.a
#     lib/omega.ld      # Omega linker script
#
# Usage: scripts/build_musl_sysroot.sh [x86_64|aarch64|riscv64] [--rebuild]
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${1:-x86_64}"
REBUILD="${2:-}"

MUSL_SRC="${PROJECT_ROOT}/libc/musl"
SHIM_SRC="${PROJECT_ROOT}/libc/omega-shim"
SDK_ROOT="${PROJECT_ROOT}/libc/omega-sdk"
OUT="${SDK_ROOT}/${ARCH}"

CLANG_BIN="${CLANG_BIN:-clang}"
LLD_BIN="${LLD_BIN:-ld.lld}"
# Prefer Homebrew LLVM tools (they may not be on PATH).
for tool in clang llvm-ar llvm-ranlib ld.lld; do
    if ! command -v "$tool" >/dev/null 2>&1 && [[ -x "/opt/homebrew/opt/llvm/bin/$tool" ]]; then
        case "$tool" in
            clang) CLANG_BIN="/opt/homebrew/opt/llvm/bin/clang" ;;
            llvm-ar) AR_BIN="/opt/homebrew/opt/llvm/bin/llvm-ar" ;;
            llvm-ranlib) RANLIB_BIN="/opt/homebrew/opt/llvm/bin/llvm-ranlib" ;;
            ld.lld) LLD_BIN="/opt/homebrew/opt/llvm/bin/ld.lld" ;;
        esac
    fi
done
if [[ -x "/opt/homebrew/opt/llvm/bin/clang" && "${CLANG_BIN}" == "clang" ]]; then
    CLANG_BIN="/opt/homebrew/opt/llvm/bin/clang"
fi
AR_BIN="${AR_BIN:-$(command -v llvm-ar || echo ar)}"
RANLIB_BIN="${RANLIB_BIN:-$(command -v llvm-ranlib || echo ranlib)}"

case "${ARCH}" in
    x86_64)
        TARGET=x86_64-unknown-none-elf
        ARCH_NAME=x86_64
        EXTRA="-mno-red-zone -mcmodel=large"
        USER_BASE=0x400000000000
        ;;
    aarch64)
        TARGET=aarch64-unknown-none-elf
        RUNTIME_TARGET=aarch64-unknown-none-elf
        ARCH_NAME=aarch64
        EXTRA=""
        USER_BASE=0x4000000000
        ;;
    riscv64)
        TARGET=riscv64-unknown-none-elf
        RUNTIME_TARGET=riscv64-unknown-none-elf
        ARCH_NAME=riscv64
        EXTRA="-march=rv64gc -mabi=lp64d -mcmodel=medany"
        USER_BASE=0x40000000
        ;;
    *) echo "unsupported arch: ${ARCH}" >&2; exit 2 ;;
esac

RUNTIME_ARCHIVE=""
RUNTIME_DIR="$(${CLANG_BIN} --target=${TARGET} --print-resource-dir 2>/dev/null)/lib/${RUNTIME_TARGET:-${TARGET}}"
if [[ -f "${RUNTIME_DIR}/libclang_rt.builtins.a" ]]; then
    RUNTIME_ARCHIVE="${RUNTIME_DIR}/libclang_rt.builtins.a"
fi

echo "[musl] Building sysroot for ${ARCH} (target ${TARGET})"

if [[ ! -d "${MUSL_SRC}/src" ]]; then
    echo "[musl] ERROR: musl source not found at ${MUSL_SRC}" >&2
    echo "[musl] Clone it first: git clone --depth 1 --branch v1.2.5 https://github.com/ifduyue/musl.git libc/musl" >&2
    exit 1
fi

mkdir -p "${OUT}"

# --- musl itself -----------------------------------------------------------
# musl's configure builds a native libc for the HOST compiler's target when
# given CC. We cross-compile by setting CC to clang with a bare-metal target
# and -ffreestanding. musl 1.2.5 supports this via --target=... --prefix=...
MUSL_BUILD="${PROJECT_ROOT}/build/musl-${ARCH}"
if [[ -n "${REBUILD}" || ! -f "${MUSL_BUILD}/lib/libc.a" ]]; then
    rm -rf "${MUSL_BUILD}"
    mkdir -p "${MUSL_BUILD}"
    cd "${MUSL_BUILD}"
    # musl's configure is intentionally host-independent: shell tools run on
    # the build machine while CC compiles the target objects.  Clang's target
    # triple makes this a real cross build even when no GNU cross compiler is
    # installed.
    CC="${CLANG_BIN} --target=${TARGET} -ffreestanding -fno-builtin -nostdinc ${EXTRA}" \
    AR="${AR_BIN}" \
    RANLIB="${RANLIB_BIN}" \
    CFLAGS="-O2 -fno-stack-protector" \
    "${MUSL_SRC}/configure" \
        --build="$(uname -m)-$(uname -s | tr '[:upper:]' '[:lower:]')" \
        --host="${TARGET}" \
        --target="${TARGET}" \
        --prefix="${OUT}" \
        --disable-shared \
        --enable-static \
        --syslibdir="${OUT}/lib"
    make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
fi

# --- omega-shim ------------------------------------------------------------
SHIM_OBJS=""
GEN_INC="${MUSL_BUILD}/obj/include"
for src in syscall.c startup.c; do
    obj="${MUSL_BUILD}/omega-${src%.c}.o"
    ${CLANG_BIN} --target="${TARGET}" -ffreestanding -fno-builtin -fno-stack-protector -nostdinc \
        -I"${GEN_INC}" -I"${MUSL_SRC}/include" -I"${SHIM_SRC}/include" ${EXTRA} -c "${SHIM_SRC}/src/${src}" -o "${obj}"
    SHIM_OBJS="${SHIM_OBJS} ${obj}"
done
case "${ARCH}" in
    x86_64) SYSCALL_ASM="syscall_x86_64.S" ;;
    aarch64) SYSCALL_ASM="syscall_aarch64.S" ;;
    riscv64) SYSCALL_ASM="syscall_riscv64.S" ;;
esac
shim_asm="${MUSL_BUILD}/omega-syscall.o"
${CLANG_BIN} --target="${TARGET}" -c "${SHIM_SRC}/src/${SYSCALL_ASM}" -o "${shim_asm}"

mkdir -p "${OUT}/lib" "${OUT}/include"
cp "${MUSL_BUILD}/lib/libc.a" "${OUT}/lib/libc.a"
cp "${MUSL_BUILD}/lib/crt1.o" "${OUT}/lib/crt1.o" 2>/dev/null || true
if [[ -n "${RUNTIME_ARCHIVE}" ]]; then
    cp "${RUNTIME_ARCHIVE}" "${OUT}/lib/libcompiler_rt.a"
fi
${AR_BIN} rcs "${OUT}/lib/libomega-shim.a" ${SHIM_OBJS} "${shim_asm}" 2>/dev/null || \
    ar rcs "${OUT}/lib/libomega-shim.a" ${SHIM_OBJS} "${shim_asm}"

# Headers: musl's include tree + generated arch bits + arch-specific bits.
rm -rf "${OUT}/include"
cp -R "${MUSL_SRC}/include" "${OUT}/include"
mkdir -p "${OUT}/include/bits"
if [[ -d "${MUSL_BUILD}/obj/include/bits" ]]; then
    cp -R "${MUSL_BUILD}/obj/include/bits/." "${OUT}/include/bits/"
fi
if [[ -d "${MUSL_SRC}/arch/${ARCH_NAME}/bits" ]]; then
    cp -R "${MUSL_SRC}/arch/${ARCH_NAME}/bits/." "${OUT}/include/bits/"
fi
if [[ -d "${MUSL_SRC}/arch/generic/bits" ]]; then
    cp -R "${MUSL_SRC}/arch/generic/bits/." "${OUT}/include/bits/"
fi

# --- linker script ----------------------------------------------------------
cat > "${OUT}/lib/omega.ld" <<EOF
/* Omega static userland linker script (${ARCH}). */
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); }
SECTIONS {
    . = ${USER_BASE};
    /* Static binary: crt1 references _DYNAMIC only for the PIE path; a
       static non-PIE build has no dynamic section. Define it at the start
       of .data so the PC32 relocation is in range (never dereferenced). */
    .text : { *(.text*) } :text
    .rodata : { *(.rodata*) } :text
    .data : {
        _DYNAMIC = .;
        *(.data*) *(.sdata*) *(.ldata*)
    } :data
    .bss : { *(.bss*) *(.sbss*) *(.lbss*) *(COMMON) } :data
}
EOF

# A small machine-readable manifest lets the TCC and SDK build steps verify
# that they are consuming a sysroot for the requested ISA, rather than a
# stale archive from another build.
RUNTIME_JSON=""
if [[ -n "${RUNTIME_ARCHIVE}" ]]; then
    RUNTIME_JSON=', "lib/libcompiler_rt.a"'
fi
cat > "${OUT}/omega-sdk.json" <<EOF
{
  "name": "omega-posix-static",
  "architecture": "${ARCH}",
  "target": "${TARGET}",
  "musl_version": "$(cat "${MUSL_SRC}/VERSION")",
  "static": true,
  "dynamic_linking": false,
  "artifacts": ["include", "lib/libc.a", "lib/crt1.o", "lib/libomega-shim.a", "lib/omega.ld"${RUNTIME_JSON}]
}
EOF

echo "[musl] Sysroot ready: ${OUT}"
ls -la "${OUT}/lib/libc.a" 2>/dev/null || echo "[musl] WARNING: libc.a missing — musl build may have failed"
