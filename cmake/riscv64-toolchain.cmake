set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

# Skip compiler checks for bare-metal cross compilation
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Find system Clang/Clang++ executable dynamically (compatible with macOS Homebrew & Linux Alpine/Ubuntu)
if(EXISTS "/opt/homebrew/opt/llvm/bin/clang")
    set(CMAKE_C_COMPILER /opt/homebrew/opt/llvm/bin/clang)
    set(CMAKE_CXX_COMPILER /opt/homebrew/opt/llvm/bin/clang++)
    set(CMAKE_ASM_COMPILER /opt/homebrew/opt/llvm/bin/clang)
else()
    find_program(CLANG_PATH NAMES clang-17 clang)
    find_program(CLANGXX_PATH NAMES clang++-17 clang++)
    set(CMAKE_C_COMPILER ${CLANG_PATH})
    set(CMAKE_CXX_COMPILER ${CLANGXX_PATH})
    set(CMAKE_ASM_COMPILER ${CLANG_PATH})
endif()

set(CMAKE_C_COMPILER_TARGET riscv64-unknown-none-elf)
set(CMAKE_CXX_COMPILER_TARGET riscv64-unknown-none-elf)
set(CMAKE_ASM_COMPILER_TARGET riscv64-unknown-none-elf)

# Use mcmodel=medany so PC-relative relocations can span the entire 64-bit space
set(CMAKE_C_FLAGS "-march=rv64gc -mabi=lp64d -mcmodel=medany")
set(CMAKE_CXX_FLAGS "-march=rv64gc -mabi=lp64d -mcmodel=medany")
set(CMAKE_ASM_FLAGS "-march=rv64gc -mabi=lp64d -mcmodel=medany")
