cmake_minimum_required(VERSION 3.20)
project(OmegaKernel C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ARCH "Target Architecture (x86_64 or aarch64)" "x86_64")
message(STATUS "Configuring Omega Kernel for Architecture: ${ARCH}")

find_program(LLD_PATH ld.lld PATHS /opt/homebrew/bin /usr/local/bin)
if(NOT LLD_PATH)
    set(LLD_PATH "ld.lld")
endif()

message(STATUS "Using LLVM Linker: ${LLD_PATH}")

set(FREESTANDING_FLAGS
    -ffreestanding
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics
    -fno-use-cxa-atexit
    -nostdlib
    -nostdinc
    -Wall
    -Wextra
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
)

set(KERNEL_SOURCES
    kernel/init/main.cpp
    kernel/sys/memory.cpp
    kernel/sys/kprint.cpp
    kernel/sys/pmm.cpp
    kernel/sys/vmm.cpp
    kernel/sys/heap.cpp
    kernel/sys/scheduler.cpp
    kernel/sys/syscall.cpp
    kernel/sys/vfs.cpp
    kernel/sys/initrd.cpp
    kernel/sys/userland.cpp
)

if(ARCH STREQUAL "x86_64")
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/kernel/arch/x86_64/linker.ld")
    list(APPEND KERNEL_SOURCES
        kernel/arch/x86_64/boot.s
        kernel/arch/x86_64/serial.cpp
        kernel/arch/x86_64/idt.cpp
    )
elseif(ARCH STREQUAL "aarch64")
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/kernel/arch/aarch64/linker.ld")
    list(APPEND KERNEL_SOURCES
        kernel/arch/aarch64/boot.s
        kernel/arch/aarch64/vectors.s
        kernel/arch/aarch64/uart.cpp
        kernel/arch/aarch64/gic.cpp
    )
else()
    message(FATAL_ERROR "Unsupported Architecture: ${ARCH}. Choose x86_64 or aarch64.")
endif()

add_compile_options(${FREESTANDING_FLAGS})

add_library(kernel_objs OBJECT ${KERNEL_SOURCES})

add_custom_target(omega.elf ALL
    COMMAND ${LLD_PATH} -T ${LINKER_SCRIPT} $<TARGET_OBJECTS:kernel_objs> -o ${CMAKE_BINARY_DIR}/omega.elf
    COMMAND_EXPAND_LISTS
    DEPENDS kernel_objs
    COMMENT "Linking Omega kernel ELF binary with ld.lld"
)
