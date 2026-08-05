set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Skip compiler checks for bare-metal cross compilation
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)

set(CMAKE_C_COMPILER_TARGET aarch64-unknown-none-elf)
set(CMAKE_CXX_COMPILER_TARGET aarch64-unknown-none-elf)
set(CMAKE_ASM_COMPILER_TARGET aarch64-unknown-none-elf)
