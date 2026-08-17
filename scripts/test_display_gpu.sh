#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

for arch in aarch64 riscv64; do
    build_dir="$PROJECT_ROOT/build/${arch}-gpu-review"
    cmake -S "$PROJECT_ROOT" -B "$build_dir" \
        -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/${arch}-toolchain.cmake" \
        -DARCH="$arch" -DENABLE_EXPERIMENTAL_VIRTIO_GPU=ON >/dev/null
    cmake --build "$build_dir" >/dev/null
done

for arch in aarch64 riscv64; do
    if [[ "${arch}" == "aarch64" ]]; then
        qemu_bin=qemu-system-aarch64
        qemu_machine="-M virt -cpu cortex-a57 -nographic"
    else
        qemu_bin=qemu-system-riscv64
        qemu_machine="-M virt -cpu rv64 -bios default -nographic"
    fi
    device_model=""
    if "${qemu_bin}" -device help 2>&1 | grep -Fq 'name "virtio-gpu-device"'; then
        device_model=virtio-gpu-device
    elif "${qemu_bin}" -device help 2>&1 | grep -Fq 'name "virtio-gpu-pci"'; then
        device_model=virtio-gpu-pci
    else
        echo "[SKIP] ${arch} VirtIO-GPU probe (QEMU has no supported VirtIO-GPU device model)"
        continue
    fi
    log_file="$PROJECT_ROOT/build/${arch}_gpu_display_test.log"
    "${qemu_bin}" ${qemu_machine} \
        -kernel "$PROJECT_ROOT/build/${arch}-gpu-review/omega.elf" \
        -device "${device_model}" >"$log_file" 2>&1 &
    qemu_pid=$!
    # GitHub-hosted runners can take longer to initialize QEMU than a local
    # workstation, especially for the AArch64 and RISC-V virtio-gpu device.
    sleep 5
    kill -9 "$qemu_pid" 2>/dev/null || true
    if ! grep -Fq "System online. Entering idle loop" "$log_file"; then
        echo "[FAIL] ${arch} VirtIO-GPU probe did not reach the idle loop" >&2
        cat "$log_file" >&2
        exit 1
    fi
done

echo "[PASS] Experimental VirtIO-GPU probes fail safely without a valid DTB handoff"
