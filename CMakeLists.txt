cmake_minimum_required(VERSION 3.20)
project(OmegaKernel C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ARCH "Target Architecture (x86_64, aarch64, riscv64)" "x86_64")
option(ENABLE_EXPERIMENTAL_VIRTIO_GPU "Enable the in-kernel VirtIO-GPU MMIO bring-up path" OFF)
option(ENABLE_EXPERIMENTAL_VIRTIO_BLOCK "Enable the in-kernel VirtIO-Block MMIO bring-up path" OFF)
option(ENABLE_SCHEDULER_SELF_TEST "Run the x86_64 timer/context-switch self-test" OFF)
option(OMEGA_BUILD_USERSPACE "Build the Omega musl sysroot and userspace tools" OFF)
option(OMEGA_BUILD_TCC "Build the target TinyCC binary when userspace is enabled" OFF)
option(OMEGA_BUILD_BASH "Build the static non-interactive Bash profile when userspace is enabled" OFF)
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
    kernel/sys/process.cpp
    kernel/sys/security.cpp
    kernel/sys/syscall.cpp
    kernel/sys/input.cpp
    kernel/sys/vfs.cpp
    kernel/sys/tmpfs.cpp
    kernel/sys/ext4.cpp
    kernel/sys/initrd.cpp
    kernel/sys/dma.cpp
    kernel/sys/storage.cpp
    kernel/sys/memory_block.cpp
    kernel/sys/partition.cpp
    kernel/sys/virtio_blk.cpp
    kernel/sys/ahci.cpp
    kernel/sys/nvme.cpp
    kernel/sys/userland.cpp
    kernel/sys/elf_loader.cpp
    kernel/sys/net.cpp
    kernel/sys/framebuffer.cpp
    kernel/sys/font.cpp
    kernel/sys/display_console.cpp
    kernel/sys/fdt.cpp
    kernel/sys/virtio_gpu.cpp
)

if(ARCH STREQUAL "x86_64")
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/kernel/arch/x86_64/linker.ld")
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/kernel/arch/x86_64)
    list(APPEND KERNEL_SOURCES
        kernel/arch/x86_64/boot.s
        kernel/arch/x86_64/serial.cpp
        kernel/arch/x86_64/idt.cpp
        kernel/arch/x86_64/input.cpp
        kernel/arch/x86_64/timer.cpp
        kernel/arch/x86_64/timer.s
        kernel/arch/x86_64/user_entry.s
        kernel/arch/x86_64/pci.cpp
        kernel/arch/x86_64/vga_regs.cpp
        kernel/arch/x86_64/vga_text.cpp
        kernel/arch/x86_64/bochs_vbe.cpp
        kernel/arch/x86_64/boot_fb.cpp
        kernel/arch/x86_64/display.cpp
    )
elseif(ARCH STREQUAL "aarch64")
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/kernel/arch/aarch64/linker.ld")
    list(APPEND KERNEL_SOURCES
        kernel/arch/aarch64/boot.s
        kernel/arch/aarch64/vectors.s
        kernel/arch/aarch64/uart.cpp
        kernel/arch/aarch64/gic.cpp
        kernel/arch/aarch64/input.cpp
        kernel/arch/aarch64/pci.cpp
        kernel/arch/aarch64/display.cpp
    )
elseif(ARCH STREQUAL "riscv64")
    set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/kernel/arch/riscv64/linker.ld")
    list(APPEND KERNEL_SOURCES
        kernel/arch/riscv64/boot.s
        kernel/arch/riscv64/trap.s
        kernel/arch/riscv64/uart.cpp
        kernel/arch/riscv64/plic.cpp
        kernel/arch/riscv64/input.cpp
        kernel/arch/riscv64/pci.cpp
        kernel/arch/riscv64/display.cpp
    )
else()
    message(FATAL_ERROR "Unsupported Architecture: ${ARCH}. Choose x86_64, aarch64, or riscv64.")
endif()

add_compile_options(${FREESTANDING_FLAGS})
if(ARCH STREQUAL "x86_64")
    # Interrupts may arrive between instructions; kernel code must not use the
    # user-space red zone below RSP.
    add_compile_options(-mno-red-zone)
endif()

add_library(kernel_objs OBJECT ${KERNEL_SOURCES})

if(ENABLE_EXPERIMENTAL_VIRTIO_GPU)
    target_compile_definitions(kernel_objs PRIVATE OMEGA_ENABLE_EXPERIMENTAL_VIRTIO_GPU=1)
endif()

if(ENABLE_SCHEDULER_SELF_TEST)
    target_compile_definitions(kernel_objs PRIVATE OMEGA_ENABLE_SCHEDULER_SELF_TEST=1)
endif()

add_custom_target(omega.elf ALL
    COMMAND ${LLD_PATH} -T ${LINKER_SCRIPT} $<TARGET_OBJECTS:kernel_objs> -o ${CMAKE_BINARY_DIR}/omega.elf
    COMMAND_EXPAND_LISTS
    DEPENDS kernel_objs
    COMMENT "Linking Omega kernel ELF binary with ld.lld"
)

if(OMEGA_BUILD_USERSPACE)
    add_custom_target(omega-libc-${ARCH} ALL
        COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_musl_sysroot.sh" "${ARCH}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building Omega musl sysroot for ${ARCH}"
        VERBATIM)

    if(OMEGA_BUILD_TCC)
        add_custom_target(omega-tcc-${ARCH} ALL
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_tcc.sh" "${ARCH}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            DEPENDS omega-libc-${ARCH}
            COMMENT "Building TinyCC for Omega ${ARCH}"
            VERBATIM)
        add_custom_target(omega-userspace ALL DEPENDS omega-tcc-${ARCH})
    elseif(OMEGA_BUILD_BASH)
        add_custom_target(omega-bash-${ARCH} ALL
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_bash.sh" "${ARCH}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            DEPENDS omega-libc-${ARCH}
            COMMENT "Building static non-interactive Bash for Omega ${ARCH}"
            VERBATIM)
        add_custom_target(omega-userspace ALL DEPENDS omega-bash-${ARCH})
    else()
        add_custom_target(omega-userspace ALL DEPENDS omega-libc-${ARCH})
    endif()
endif()
