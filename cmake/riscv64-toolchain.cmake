set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

# Skip compiler checks for bare-metal cross compilation
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Use Homebrew LLVM Clang which contains the RISC-V 64 backend target
set(CMAKE_C_COMPILER /opt/homebrew/opt/llvm/bin/clang)
set(CMAKE_CXX_COMPILER /opt/homebrew/opt/llvm/bin/clang++)
set(CMAKE_ASM_COMPILER /opt/homebrew/opt/llvm/bin/clang)

set(CMAKE_C_COMPILER_TARGET riscv64-unknown-none-elf)
set(CMAKE_CXX_COMPILER_TARGET riscv64-unknown-none-elf)
set(CMAKE_ASM_COMPILER_TARGET riscv64-unknown-none-elf)

# Use mcmodel=medany so PC-relative relocations can span the entire 64-bit space
set(CMAKE_C_FLAGS "-march=rv64gc -mabi=lp64d -mcmodel=medany")
set(CMAKE_CXX_FLAGS "-march=rv64gc -mabi=lp64d -mcmodel=medany")
set(CMAKE_ASM_FLAGS "-march=rv64gc -mabi=lp64d -mcmodel=medany")
