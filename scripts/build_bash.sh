#!/usr/bin/env bash
# Build the first, non-interactive Bash profile for Omega.
#
# The initial profile intentionally excludes Readline, job control, NLS,
# process substitution, and network redirections. Those features are enabled
# only after Omega's TTY, signal, process-group, and filesystem milestones are
# complete; see docs/BASH_PORTING_PLAN.md.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="${1:-x86_64}"
REBUILD="${2:-}"
BASH_SOURCE_VERSION="${BASH_SOURCE_VERSION:-5.3}"
BASH_URL="${BASH_URL:-https://ftp.gnu.org/gnu/bash/bash-${BASH_SOURCE_VERSION}.tar.gz}"
BASH_SHA256="${BASH_SHA256:-0d5cd86965f869a26cf64f4b71be7b96f90a3ba8b3d74e27e8e9d9d5550f31ba}"

SDK="${PROJECT_ROOT}/libc/omega-sdk/${ARCH}"
BUILD="${PROJECT_ROOT}/build/bash-${ARCH}"
DOWNLOAD="${PROJECT_ROOT}/build/bash-download"
SOURCE="${BUILD}/source/bash-${BASH_SOURCE_VERSION}"
STAGE="${PROJECT_ROOT}/userland/bash/${ARCH}"

case "${ARCH}" in
    x86_64)
        TARGET=x86_64-unknown-none-elf
        CONFIG_HOST=x86_64-unknown-none-elf
        EXTRA=(-mno-red-zone -mcmodel=large)
        MACHINE=x86_64
        ;;
    aarch64)
        TARGET=aarch64-unknown-none-elf
        CONFIG_HOST=aarch64-unknown-none-elf
        EXTRA=()
        MACHINE=aarch64
        ;;
    riscv64)
        TARGET=riscv64-unknown-none-elf
        CONFIG_HOST=riscv64-unknown-none-elf
        EXTRA=(-march=rv64gc -mabi=lp64d -mcmodel=medany)
        MACHINE=riscv64
        ;;
    *)
        echo "unsupported architecture: ${ARCH}" >&2
        exit 2
        ;;
esac

CLANG_BIN="${CLANG_BIN:-clang}"
if [[ "${ARCH}" == "riscv64" && -x /opt/homebrew/opt/llvm/bin/clang && "${CLANG_BIN}" == "clang" ]]; then
    CLANG_BIN=/opt/homebrew/opt/llvm/bin/clang
fi
AR_BIN="${AR_BIN:-$(command -v llvm-ar || command -v ar)}"
RANLIB_BIN="${RANLIB_BIN:-$(command -v llvm-ranlib || command -v ranlib)}"
LD_BIN="${LD_BIN:-$(command -v ld.lld || true)}"

[[ -n "${LD_BIN}" ]] || { echo "ld.lld is required" >&2; exit 1; }
[[ -f "${SDK}/lib/libc.a" ]] || bash "${PROJECT_ROOT}/scripts/build_musl_sysroot.sh" "${ARCH}"
RUNTIME_LIB=""
if [[ -f "${SDK}/lib/libcompiler_rt.a" ]]; then
    RUNTIME_LIB="${SDK}/lib/libcompiler_rt.a"
elif [[ -f "${SDK}/lib/libtcc1.a" ]]; then
    RUNTIME_LIB="${SDK}/lib/libtcc1.a"
elif [[ "${ARCH}" != "x86_64" ]]; then
    bash "${PROJECT_ROOT}/scripts/build_tcc.sh" "${ARCH}"
    [[ -f "${SDK}/lib/libtcc1.a" ]] || {
        echo "target compiler runtime is missing: ${SDK}/lib/libtcc1.a" >&2
        exit 1
    }
    RUNTIME_LIB="${SDK}/lib/libtcc1.a"
fi

download_source() {
    local archive="${DOWNLOAD}/bash-${BASH_SOURCE_VERSION}.tar.gz"
    mkdir -p "${DOWNLOAD}"
    if [[ ! -f "${archive}" || "${REBUILD}" == "--rebuild" ]]; then
        command -v curl >/dev/null 2>&1 || {
            echo "curl is required to download Bash ${BASH_SOURCE_VERSION}" >&2
            exit 1
        }
        curl -L --fail --silent --show-error "${BASH_URL}" -o "${archive}.tmp"
        mv "${archive}.tmp" "${archive}"
    fi
    local actual
    actual="$(shasum -a 256 "${archive}" | awk '{print $1}')"
    if [[ "${actual}" != "${BASH_SHA256}" ]]; then
        echo "Bash source checksum mismatch" >&2
        echo "  expected: ${BASH_SHA256}" >&2
        echo "  actual:   ${actual}" >&2
        exit 1
    fi
    rm -rf "${BUILD}/source"
    mkdir -p "${BUILD}/source"
    tar -xzf "${archive}" -C "${BUILD}/source"
}

if [[ "${REBUILD}" == "--rebuild" || ! -f "${SOURCE}/configure" ]]; then
    download_source
fi
[[ -f "${SOURCE}/configure" ]] || {
    echo "Bash source is missing: ${SOURCE}" >&2
    exit 1
}

if [[ -z "${CLANG_BIN}" ]]; then
    echo "clang is required" >&2
    exit 1
fi

if [[ "${REBUILD}" == "--rebuild" || ! -x "${BUILD}/obj/bash" ]]; then
    rm -rf "${BUILD}/obj"
    mkdir -p "${BUILD}/obj"
    cd "${BUILD}/obj"

    # Bash's configure creates host tools such as mkbuiltins. BUILD_CC remains
    # the host compiler; CC is an Omega-target wrapper used only for target
    # objects and the final target link.
    OMEGA_CC="${BUILD}/omega-cc"
    cat > "${OMEGA_CC}" <<EOF
#!/usr/bin/env bash
set -euo pipefail
args=("\$@")
has_compile=0
output=a.out
inputs=()
sources=()
libdirs=(.)
for ((i=0; i<\${#args[@]}; i++)); do
    case "\${args[i]}" in
        -c|-S|-E) has_compile=1 ;;
        -o) ((i++)); output="\${args[i]}" ;;
        *.o|*.a) inputs+=("\${args[i]}") ;;
        *.c|*.S|*.s) sources+=("\${args[i]}") ;;
        -L*) libdirs+=("\${args[i]#-L}") ;;
        -l*)
            case "\${args[i]}" in
                -lc) inputs+=("${SDK}/lib/libc.a") ;;
                -lcompiler_rt) inputs+=("${SDK}/lib/libcompiler_rt.a") ;;
                *)
                    library="\${args[i]#-l}"
                    for directory in "\${libdirs[@]}"; do
                        candidate="\${directory}/lib\${library}.a"
                        if [[ -f "\${candidate}" ]]; then
                            inputs+=("\${candidate}")
                            break
                        fi
                    done
                    ;;
            esac
            ;;
esac
done
case "\${args[0]-}" in
    --version|-v|-V|-qversion|-version)
        exec "${CLANG_BIN}" --version
        ;;
esac
if ((\${has_compile})); then
    exec "${CLANG_BIN}" --target=${TARGET} -ffreestanding -fno-stack-protector \\
        -fno-builtin -nostdinc -isystem "${SDK}/include" ${EXTRA[*]-} "\$@"
fi
if ((\${#sources[@]})); then
    temporary="\${output}.omega.o"
    "${CLANG_BIN}" --target=${TARGET} -ffreestanding -fno-stack-protector \\
        -fno-builtin -nostdinc -isystem "${SDK}/include" ${EXTRA[*]-} \\
        -c "\${sources[0]}" -o "\${temporary}"
    inputs+=("\${temporary}")
fi
exec "${LD_BIN}" -flavor gnu -T "${SDK}/lib/omega.ld" -o "\${output}" \\
    "${SDK}/lib/crt1.o" "\${inputs[@]-}" "${SDK}/lib/libc.a" \\
    "${SDK}/lib/libomega-shim.a" ${RUNTIME_LIB}
EOF
    chmod +x "${OMEGA_CC}"

    # Configure with a target host, but never run a target executable during
    # the build. The minimal profile removes terminal and dynamic features;
    # the configure switches are intentionally explicit for auditability.
    BUILD_CC="${BUILD_CC:-clang}" \
    CC="${OMEGA_CC}" \
    AR="${AR_BIN}" \
    RANLIB="${RANLIB_BIN}" \
    "${SOURCE}/configure" \
        --build="$(uname -m)-$(uname -s | tr '[:upper:]' '[:lower:]')" \
        --host="${CONFIG_HOST}" \
        --target="${CONFIG_HOST}" \
        --enable-minimal-config \
        --disable-readline \
        --disable-job-control \
        --without-bash-malloc \
        --disable-nls \
        --disable-process-substitution \
        --disable-net-redirections \
        --enable-static-link \
        ac_cv_func_asprintf=yes \
        ac_cv_func_dprintf=yes \
        ac_cv_func_snprintf=yes \
        ac_cv_func_strftime=yes \
        ac_cv_func_strtod=yes \
        ac_cv_func_syslog=yes \
        ac_cv_func_vasprintf=yes \
        ac_cv_func_vprintf=yes \
        ac_cv_func_vsnprintf=yes \
        bash_cv_job_control_missing=yes \
        bash_cv_sys_siglist=yes

    cat > config-extra.mak <<EOF
# Omega static target link policy.
CC=${OMEGA_CC}
AR=${AR_BIN}
RANLIB=${RANLIB_BIN}
CFLAGS += -ffreestanding -fno-stack-protector -fno-builtin -nostdinc -isystem ${SDK}/include ${EXTRA[*]-}
LDFLAGS += -nostdlib -static
EOF
    make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" bash
fi

mkdir -p "${STAGE}/bin" "${STAGE}/licenses/bash"
cp "${BUILD}/obj/bash" "${STAGE}/bin/bash"
cp "${SOURCE}/COPYING" "${STAGE}/licenses/bash/COPYING"
cat > "${STAGE}/manifest.json" <<EOF
{
  "name": "omega-bash",
  "version": "${BASH_SOURCE_VERSION}",
  "architecture": "${ARCH}",
  "target": "${TARGET}",
  "static": true,
  "interactive": false,
  "readline": false,
  "job_control": false,
  "source_sha256": "${BASH_SHA256}"
}
EOF
echo "[bash] Omega ${ARCH} Bash: ${STAGE}/bin/bash"
