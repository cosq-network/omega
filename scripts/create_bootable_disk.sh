#!/usr/bin/env bash

# Generate architecture-specific GPT boot images with a FAT32 ESP and an
# ext4 root partition. The installed-root namespace follows
# docs/ROOT_FILESYSTEM_LAYOUT_PLAN.md. --legacy-fat remains available for
# older QEMU/image tests that expect the pre-GPT single-volume format.

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_ROOT="${OMEGA_BUILD_ROOT:-${PROJECT_ROOT}/build}"
IMAGE_ROOT="${OMEGA_IMAGE_ROOT:-${PROJECT_ROOT}/disk_images}"
SIZE_MB=512
NO_BUILD=false
DRY_RUN=false
LEGACY_FAT=false
ARCHES=(x86_64 aarch64 riscv64)

usage() {
    cat <<USAGE
Usage: $0 [options]
  --arch ARCH             Generate only x86_64, aarch64, or riscv64
  --size MB               Raw image size in MiB (default: 64)
  --output-dir DIR        Image output directory
  --legacy-fat            Use the pre-GPT single FAT-compatible image format
  --no-build              Require existing kernel ELF files
  --dry-run               Print planned operations without changing files
USAGE
    exit 1
}

while (($#)); do
    case "$1" in
        --arch) ARCHES=("${2:-}"); shift 2;; --size) SIZE_MB="${2:-}"; shift 2;;
        --output-dir) IMAGE_ROOT="${2:-}"; shift 2;; --legacy-fat) LEGACY_FAT=true; shift;;
        --no-build) NO_BUILD=true; shift;;
        --dry-run) DRY_RUN=true; shift;; *) usage;;
    esac
done
[[ "${SIZE_MB}" =~ ^[0-9]+$ ]] && (( SIZE_MB >= 1 && SIZE_MB <= 16777216 )) || { echo "Invalid image size: ${SIZE_MB}" >&2; exit 1; }
if [ "${LEGACY_FAT}" = false ] && (( SIZE_MB < 320 )); then
    echo "Ext4 GPT images require at least 320 MiB; use --legacy-fat for smaller compatibility images." >&2
    exit 1
fi
for arch in "${ARCHES[@]}"; do case "${arch}" in x86_64|aarch64|riscv64) ;; *) echo "Unsupported architecture: ${arch}" >&2; exit 1;; esac; done

efi_name() { case "$1" in x86_64) echo BOOTX64.EFI;; aarch64) echo BOOTAA64.EFI;; riscv64) echo BOOTRISCV64.EFI;; esac; }

require_tool() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "Required host tool missing: $1 (install e2fsprogs, dosfstools, and gdisk)" >&2
        return 1
    }
}

populate_ext4() {
    local image="$1" source="$2" commands_file="$3" relative mode
    : > "${commands_file}"
    while IFS= read -r relative; do
        relative="${relative#${source}/}"
        printf 'mkdir /%s\n' "${relative}" >> "${commands_file}"
    done < <(find "${source}" -type d -mindepth 1 -print | sort)
    while IFS= read -r relative; do
        relative="${relative#${source}/}"
        printf 'write %s /%s\n' "${source}/${relative}" "${relative}" >> "${commands_file}"
        mode=0100644
        if [ -x "${source}/${relative}" ]; then mode=0100755; fi
        printf 'set_inode_field /%s mode %s\n' "${relative}" "${mode}" >> "${commands_file}"
    done < <(find "${source}" -type f -mindepth 1 -print | sort)
    debugfs -w -f "${commands_file}" "${image}" >/dev/null
}

populate_fat() {
    local image="$1" source="$2" directory
    mformat -i "${image}" ::
    copy_fat_payload "${image}" "${source}"
}

copy_fat_payload() {
    local image="$1" source="$2" directory
    while IFS= read -r directory; do
        directory="${directory#${source}/}"
        mmd -i "${image}" "::/${directory}"
    done < <(find "${source}" -type d -mindepth 1 -print | sort)
    mcopy -s -i "${image}" "${source}"/* ::/
}

build_arch_image() {
    local arch="$1" efi="$(efi_name "$1")" build_dir="${BUILD_ROOT}/${arch}"
    local raw="${IMAGE_ROOT}/omega-${arch}-bootable.img"
    local stage="${IMAGE_ROOT}/rootfs-${arch}"
    local sdk="${PROJECT_ROOT}/libc/omega-sdk/${arch}"
    local commands="${PROJECT_ROOT}/userland/commands/${arch}/bin"
    local tcc="${sdk}/bin/tcc"
    echo "[*] Planning boot image: ${arch} (${SIZE_MB} MiB)"
    if [ "${DRY_RUN}" = true ]; then
        echo "    ELF: ${build_dir}/omega.elf"
        echo "    SDK: ${sdk}/include + ${sdk}/lib"
        echo "    TCC: ${tcc} -> /usr/bin/tcc"
        echo "    POSIX: ${commands} -> /bin"
        echo "    ROOTFS: ${stage}"
        echo "    RAW: ${raw}"
        return
    fi
    if [ "${NO_BUILD}" = false ]; then
        cmake -S "${PROJECT_ROOT}" -B "${build_dir}" \
            -DCMAKE_TOOLCHAIN_FILE="${PROJECT_ROOT}/cmake/${arch}-toolchain.cmake" -DARCH="${arch}"
        cmake --build "${build_dir}"
    fi
    [ -s "${build_dir}/omega.elf" ] || { echo "Kernel ELF missing: ${build_dir}/omega.elf" >&2; return 1; }

    # The installed image is self-contained: build or validate the matching
    # musl sysroot, TinyCC, and every command in the current POSIX port.
    if [ "${NO_BUILD}" = false ]; then
        bash "${PROJECT_ROOT}/scripts/build_musl_sysroot.sh" "${arch}"
        bash "${PROJECT_ROOT}/scripts/build_tcc.sh" "${arch}"
        bash "${PROJECT_ROOT}/scripts/build_commands.sh" "${arch}"
    fi
    [ -d "${sdk}/include" ] || { echo "musl headers missing: ${sdk}/include" >&2; return 1; }
    [ -f "${sdk}/lib/libc.a" ] || { echo "musl libc missing: ${sdk}/lib/libc.a" >&2; return 1; }
    [ -f "${sdk}/lib/crt1.o" ] || { echo "musl CRT missing: ${sdk}/lib/crt1.o" >&2; return 1; }
    [ -x "${tcc}" ] || { echo "Omega TinyCC missing: ${tcc}" >&2; return 1; }
    [ -d "${commands}" ] || { echo "POSIX command directory missing: ${commands}" >&2; return 1; }

    local command
    local -a posix_commands=(ls dir ln pwd cat mkdir rm rmdir mv echo true false env test)
    for command in "${posix_commands[@]}"; do
        [ -x "${commands}/${command}" ] || {
            echo "POSIX command missing: ${commands}/${command}" >&2
            return 1
        }
    done

    mkdir -p "${IMAGE_ROOT}"
    rm -rf "${stage}"
    mkdir -p "${stage}"/{bin,boot,dev,etc/omega,home,lib,media,mnt,opt,proc,root,run,sbin,srv,sys,tmp,usr/bin,usr/include,usr/lib,usr/libexec,usr/local,usr/share,var/cache,var/lib/omega,var/log,var/run,var/spool,var/tmp,EFI/BOOT}

    # Boot artifacts and release metadata.
    cp "${build_dir}/omega.elf" "${stage}/boot/omega.elf"
    cp "${build_dir}/omega.elf" "${stage}/EFI/BOOT/${efi}"
    cat > "${stage}/etc/omega/installation.json" <<EOF
{
  "name": "omega",
  "architecture": "${arch}",
  "root_profile": "static-development",
  "kernel": "/boot/omega.elf",
  "bootloader": "/EFI/BOOT/${efi}",
  "sdk": "/usr",
  "static": true,
  "dynamic_linking": false,
  "posix_commands": ["ls", "dir", "ln", "pwd", "cat", "mkdir", "rm", "rmdir", "mv", "echo", "true", "false", "env", "test"],
  "compiler": "/usr/bin/tcc"
}
EOF
    cat > "${stage}/etc/profile" <<'EOF'
PATH=/bin:/usr/bin:/sbin:/usr/sbin
export PATH
EOF

    # Install all currently ported commands. Keep /bin the canonical runtime
    # location; the SDK and compiler live below /usr for the developer image.
    cp "${commands}"/* "${stage}/bin/"
    cp -R "${sdk}/include/." "${stage}/usr/include/"
    cp -R "${sdk}/lib/." "${stage}/usr/lib/"
    cp "${tcc}" "${stage}/usr/bin/tcc"
    chmod 0755 "${stage}/usr/bin/tcc" "${stage}/bin"/*

    # TCC's current Omega configuration uses /include and /lib. Provide
    # compatibility copies while the installed SDK converges on /usr/include
    # and /usr/lib; this also makes the image usable by the on-target plan.
    cp -R "${sdk}/include/." "${stage}/include/"
    cp -R "${sdk}/lib/." "${stage}/lib/"

    if [ "${LEGACY_FAT}" = true ]; then
        require_tool mformat
        require_tool mcopy
        dd if=/dev/zero of="${raw}" bs=1M count="${SIZE_MB}" status=none
        populate_fat "${raw}" "${stage}"
        echo "    [PASS] Installed root payload copied to legacy FAT image"
    else
        require_tool sgdisk
        require_tool debugfs
        require_tool mkfs.ext4
        require_tool mkfs.vfat || require_tool mkfs.fat
        require_tool mformat
        require_tool mcopy

        local esp_mb=256 root_mb=$((SIZE_MB - 258))
        local esp_start=2048 root_start=$((esp_start + esp_mb * 2048))
        local temp_dir esp_image root_image debugfs_commands root_stage esp_stage
        temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/omega-disk.XXXXXX")"
        esp_image="${temp_dir}/esp.img"
        root_image="${temp_dir}/root.ext4"
        debugfs_commands="${temp_dir}/debugfs.commands"
        root_stage="${temp_dir}/rootfs"
        esp_stage="${temp_dir}/esp"

        # /boot and the EFI files live on the FAT32 ESP. The persistent root
        # receives the POSIX namespace and the development SDK payload.
        mkdir -p "${esp_stage}/EFI/BOOT" "${esp_stage}/boot"
        cp "${stage}/EFI/BOOT/${efi}" "${esp_stage}/EFI/BOOT/${efi}"
        cp "${stage}/boot/omega.elf" "${esp_stage}/boot/omega.elf"
        cp -R "${stage}" "${root_stage}"
        rm -rf "${root_stage}/EFI" "${root_stage}/boot"
        mkdir -p "${root_stage}/boot"

        dd if=/dev/zero of="${raw}" bs=1M count="${SIZE_MB}" status=none
        sgdisk --zap-all --clear \
            --new=1:"${esp_start}":+"${esp_mb}"M --typecode=1:ef00 --change-name=1:Omega-ESP \
            --new=2:"${root_start}":0 --typecode=2:8300 --change-name=2:Omega-Root \
            "${raw}" >/dev/null

        dd if=/dev/zero of="${esp_image}" bs=1M count="${esp_mb}" status=none
        if command -v mkfs.vfat >/dev/null 2>&1; then
            mkfs.vfat -F 32 -n OMEGA-ESP "${esp_image}" >/dev/null
        else
            mkfs.fat -F 32 -n OMEGA-ESP "${esp_image}" >/dev/null
        fi
        copy_fat_payload "${esp_image}" "${esp_stage}"

        dd if=/dev/zero of="${root_image}" bs=1M count="${root_mb}" status=none
        mkfs.ext4 -F -L OMEGA-ROOT "${root_image}" >/dev/null
        populate_ext4 "${root_image}" "${root_stage}" "${debugfs_commands}"

        dd if="${esp_image}" of="${raw}" bs=512 seek="${esp_start}" conv=notrunc status=none
        dd if="${root_image}" of="${raw}" bs=512 seek="${root_start}" conv=notrunc status=none
        rm -rf "${temp_dir}"
        echo "    [PASS] GPT image populated: FAT32 ESP + ext4 root"
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
