#!/usr/bin/env bash
# Build TinyCC itself as a static Omega userspace program.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${1:-x86_64}"
REBUILD="${2:-}"
TCC_SRC="${PROJECT_ROOT}/tools/tcc"
SDK="${PROJECT_ROOT}/libc/omega-sdk/${ARCH}"
BUILD="${PROJECT_ROOT}/build/tcc-${ARCH}"
LLD_BIN="${LLD_BIN:-$(command -v ld.lld || true)}"
[[ -n "${LLD_BIN}" ]] || { echo "ld.lld is required to link Omega binaries" >&2; exit 1; }

case "${ARCH}" in
    x86_64) TARGET=x86_64-unknown-none-elf; EXTRA="-mno-red-zone -mcmodel=large"; CPU=x86_64 ;;
    aarch64) TARGET=aarch64-unknown-none-elf; EXTRA=""; CPU=arm64 ;;
    riscv64) TARGET=riscv64-unknown-none-elf; EXTRA="-march=rv64gc -mabi=lp64d -mcmodel=medany"; CPU=riscv64 ;;
    *) echo "unsupported architecture: ${ARCH}" >&2; exit 2 ;;
esac

if [[ -z "${CLANG_BIN:-}" && -x "/opt/homebrew/opt/llvm/bin/clang" ]]; then
    CLANG_BIN="/opt/homebrew/opt/llvm/bin/clang"
fi

[[ -f "${TCC_SRC}/configure" ]] || { echo "TinyCC source missing: ${TCC_SRC}" >&2; exit 1; }
[[ -f "${SDK}/lib/libc.a" ]] || { "${PROJECT_ROOT}/scripts/build_musl_sysroot.sh" "${ARCH}"; }

TARGET_CLANG="${CLANG_BIN:-clang}"
if [[ -n "${AR_BIN:-}" ]]; then
    TARGET_AR="${AR_BIN}"
elif [[ -x "/opt/homebrew/opt/llvm/bin/llvm-ar" ]]; then
    TARGET_AR="/opt/homebrew/opt/llvm/bin/llvm-ar"
else
    TARGET_AR="$(command -v llvm-ar || command -v ar)"
fi
RUNTIME_LIB="${SDK}/lib/libtcc1.a"

# TinyCC emits calls to compiler helper routines for floating point and
# 64-bit operations.  x86_64 currently needs none for the compiler itself,
# while the arm64/riscv64 backends do. Build the upstream freestanding
# libtcc1 sources with the same target clang used by the Omega sysroot.
if [[ "${ARCH}" != "x86_64" && ! -f "${RUNTIME_LIB}" ]]; then
    RUNTIME_BUILD="${BUILD}/runtime"
    rm -rf "${RUNTIME_BUILD}"
    mkdir -p "${RUNTIME_BUILD}"
    RUNTIME_CFLAGS=(--target="${TARGET}" -ffreestanding -fno-stack-protector -fno-builtin
        -nostdinc -isystem "${SDK}/include" -I "${TCC_SRC}/include" ${EXTRA})
    for source in lib-arm64.c stdatomic.c builtin.c dsohandle.c; do
        "${TARGET_CLANG}" "${RUNTIME_CFLAGS[@]}" -c "${TCC_SRC}/lib/${source}" \
            -o "${RUNTIME_BUILD}/${source%.c}.o"
    done
    for source in atomic.S alloca.S alloca-bt.S; do
        "${TARGET_CLANG}" "${RUNTIME_CFLAGS[@]}" -c "${TCC_SRC}/lib/${source}" \
            -o "${RUNTIME_BUILD}/${source%.*}.o"
    done
    # clang exposes the target cache-flush builtin, while the upstream
    # source names compiler-specific entry points.
    "${TARGET_CLANG}" "${RUNTIME_CFLAGS[@]}" \
        -D__arm64_clear_cache=__builtin___clear_cache \
        -D__riscv64_clear_cache=__builtin___clear_cache \
        -c "${TCC_SRC}/lib/armflush.c" -o "${RUNTIME_BUILD}/armflush.o"
    "${TARGET_AR}" rcs "${RUNTIME_LIB}" "${RUNTIME_BUILD}"/*.o
fi
RUNTIME_LINK=""
[[ -f "${RUNTIME_LIB}" ]] && RUNTIME_LINK=" ${RUNTIME_LIB}"

if [[ -n "${REBUILD}" || ! -x "${BUILD}/tcc" ]]; then
    rm -rf "${BUILD}"
    mkdir -p "${BUILD}"
    cd "${BUILD}"

    # A cross-target clang invoked on macOS still routes final links through
    # Darwin's driver.  This wrapper keeps compilation in clang and sends
    # target links directly to GNU-flavour ld.lld.
    WRAPPER="${BUILD}/omega-cc"
    cat > "${WRAPPER}" <<EOF
#!/usr/bin/env bash
set -euo pipefail
args=("\$@")
if [[ "\${1:-}" == "-ar" ]]; then
    shift
    exec "${TARGET_AR}" "\$@"
fi
has_c=0
output=a.out
objects=()
for ((i=0; i<\${#args[@]}; i++)); do
    case "\${args[i]}" in
        -c) has_c=1 ;;
        -o) ((i++)); output="\${args[i]}" ;;
        *.o|*.a) objects+=("\${args[i]}") ;;
    esac
done
if ((has_c)); then
    exec "${TARGET_CLANG}" --target=${TARGET} -ffreestanding -fno-stack-protector -fno-builtin -nostdinc -isystem "${SDK}/include" ${EXTRA} "\$@"
fi
exec "${LLD_BIN}" -flavor gnu -T "${SDK}/lib/omega.ld" -o "\${output}" "\${objects[@]-}"
EOF
    chmod +x "${WRAPPER}"

    # TCC's configure understands musl paths, but Omega is statically linked
    # and has no dynamic loader.  The extra make fragment supplies the crt,
    # libc archive, and Omega linker script to the final target link.
    CC="${WRAPPER}" AR="${TARGET_AR}" \
        "${TCC_SRC}/configure" --config-musl --enable-static \
        --targetos=Omega --cpu="${CPU}" --prefix=/usr --tccdir=/lib/tcc \
        --sysroot=/ --sysincludepaths=/include --libpaths=/lib \
        --crtprefix=/lib --extra-cflags="-D__linux__ ${EXTRA}" \
        --extra-ldflags="-nostdlib -static -fuse-ld=${LLD_BIN} -Wl,-T${SDK}/lib/omega.ld"

    cat > config-extra.mak <<EOF
# Omega target link inputs.  The command is deliberately target-specific.
AR=${TARGET_AR}
LIBS := ${SDK}/lib/crt1.o ${SDK}/lib/libc.a${RUNTIME_LINK}
CFLAGS += -D__linux__ -ffreestanding -fno-stack-protector -fno-builtin -nostdinc -isystem ${SDK}/include ${EXTRA}
LDFLAGS += -nostdlib -static -fuse-ld=${LLD_BIN} -Wl,-T${SDK}/lib/omega.ld
# c2str.exe is a build-time host helper; all other objects use the target CC.
c2str.exe: CC=clang
EOF
    make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" tcc
fi

mkdir -p "${PROJECT_ROOT}/libc/omega-sdk/${ARCH}/bin"
cp "${BUILD}/tcc" "${PROJECT_ROOT}/libc/omega-sdk/${ARCH}/bin/tcc"
echo "[tcc] Omega ${ARCH} compiler: ${BUILD}/tcc"
