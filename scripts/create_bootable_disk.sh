#!/usr/bin/env bash

# Generate architecture-specific raw/FAT32-compatible boot images and optional
# qemu-img conversions. Existing build/image directories are preserved.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${OMEGA_BUILD_ROOT:-${PROJECT_ROOT}/build}"
IMAGE_ROOT="${OMEGA_IMAGE_ROOT:-${PROJECT_ROOT}/disk_images}"
SIZE_MB=64
NO_BUILD=false
DRY_RUN=false
ARCHES=(x86_64 aarch64 riscv64)

usage() {
    cat <<USAGE
Usage: $0 [options]
  --arch ARCH             Generate only x86_64, aarch64, or riscv64
  --size MB               Raw image size in MiB (default: 64)
  --output-dir DIR        Image output directory
  --no-build              Require existing kernel ELF files
  --dry-run               Print planned operations without changing files
USAGE
    exit 1
}

while (($#)); do
    case "$1" in
        --arch) ARCHES=("${2:-}"); shift 2;; --size) SIZE_MB="${2:-}"; shift 2;;
        --output-dir) IMAGE_ROOT="${2:-}"; shift 2;; --no-build) NO_BUILD=true; shift;;
        --dry-run) DRY_RUN=true; shift;; *) usage;;
    esac
done
[[ "${SIZE_MB}" =~ ^[0-9]+$ ]] && (( SIZE_MB >= 1 && SIZE_MB <= 16777216 )) || { echo "Invalid image size: ${SIZE_MB}" >&2; exit 1; }
for arch in "${ARCHES[@]}"; do case "${arch}" in x86_64|aarch64|riscv64) ;; *) echo "Unsupported architecture: ${arch}" >&2; exit 1;; esac; done

efi_name() { case "$1" in x86_64) echo BOOTX64.EFI;; aarch64) echo BOOTAA64.EFI;; riscv64) echo BOOTRISCV64.EFI;; esac; }

build_arch_image() {
    local arch="$1" efi="$(efi_name "$1")" build_dir="${BUILD_ROOT}/${arch}"
    local raw="${IMAGE_ROOT}/omega-${arch}-bootable.img"
    echo "[*] Planning boot image: ${arch} (${SIZE_MB} MiB)"
    if [ "${DRY_RUN}" = true ]; then
        echo "    ELF: ${build_dir}/omega.elf"
        echo "    RAW: ${raw}"
        return
    fi
    if [ "${NO_BUILD}" = false ]; then
        cmake -S "${PROJECT_ROOT}" -B "${build_dir}" \
            -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/${arch}-toolchain.cmake" -DARCH="${arch}"
        cmake --build "${build_dir}"
    fi
    [ -s "${build_dir}/omega.elf" ] || { echo "Kernel ELF missing: ${build_dir}/omega.elf" >&2; return 1; }
    mkdir -p "${IMAGE_ROOT}"
    dd if=/dev/zero of="${raw}" bs=1M count="${SIZE_MB}" status=none
    if command -v mformat >/dev/null 2>&1; then
        mformat -i "${raw}" ::
        mmd -i "${raw}" ::/EFI ::/EFI/BOOT ::/boot
        mcopy -i "${raw}" "${build_dir}/omega.elf" "::/EFI/BOOT/${efi}"
        mcopy -i "${raw}" "${build_dir}/omega.elf" ::/boot/omega.elf
    else
        echo "[WARN] mtools unavailable; raw image created without FAT32 payload population." >&2
    fi
    if command -v qemu-img >/dev/null 2>&1; then
        qemu-img convert -f raw -O qcow2 "${raw}" "${IMAGE_ROOT}/omega-${arch}-bootable.qcow2"
        qemu-img convert -f raw -O vmdk "${raw}" "${IMAGE_ROOT}/omega-${arch}-bootable.vmdk"
        qemu-img convert -f raw -O vdi "${raw}" "${IMAGE_ROOT}/omega-${arch}-bootable.vdi"
    else
        echo "[WARN] qemu-img unavailable; only RAW output was generated." >&2
    fi
    echo "[PASS] Generated ${arch} boot images in ${IMAGE_ROOT}"
}

for arch in "${ARCHES[@]}"; do build_arch_image "${arch}"; done
echo "[PASS] Boot image generation completed"
