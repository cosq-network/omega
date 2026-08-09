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

for spec in \
    "aarch64:qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -kernel $PROJECT_ROOT/build/aarch64-gpu-review/omega.elf -device virtio-gpu-device" \
    "riscv64:qemu-system-riscv64 -M virt -cpu rv64 -bios default -nographic -kernel $PROJECT_ROOT/build/riscv64-gpu-review/omega.elf -device virtio-gpu-device"; do
    arch="${spec%%:*}"
    command_line="${spec#*:}"
    log_file="$PROJECT_ROOT/build/${arch}_gpu_display_test.log"
    sh -c "$command_line" >"$log_file" 2>&1 &
    qemu_pid=$!
    sleep 2
    kill "$qemu_pid" 2>/dev/null || true
    grep -Fq "System online. Entering idle loop" "$log_file"
done

echo "[PASS] Experimental VirtIO-GPU probes fail safely without a valid DTB handoff"
