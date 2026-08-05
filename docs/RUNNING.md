# Run scripts for QEMU testing of Omega Kernel

## x86_64 Emulation
```bash
# Build
cd build/x86_64 && cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/x86_64-toolchain.cmake -DARCH=x86_64 ../.. && make

# Run
qemu-system-x86_64 -kernel build/x86_64/omega.elf -serial stdio -display none
```

## AArch64 Emulation
```bash
# Build
cd build/aarch64 && cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/aarch64-toolchain.cmake -DARCH=aarch64 ../.. && make

# Run
qemu-system-aarch64 -M virt -cpu cortex-a57 -nographic -kernel build/aarch64/omega.elf
```
