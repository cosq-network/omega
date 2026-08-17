cmake_minimum_required(VERSION 3.20)

# This file is intentionally a thin façade over the upstream musl build.  The
# kernel remains a separate freestanding target; invoking the scripts here
# keeps musl's own Makefile authoritative while giving CI and IDE users a
# normal CMake entry point.
set(OMEGA_LIBC_ARCH "${ARCH}" CACHE STRING "Omega userspace architecture")
add_custom_target(omega-libc
    COMMAND "${CMAKE_SOURCE_DIR}/scripts/build_musl_sysroot.sh" "${OMEGA_LIBC_ARCH}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    VERBATIM)

if(OMEGA_BUILD_TCC)
    add_custom_target(omega-tcc
        COMMAND "${CMAKE_SOURCE_DIR}/scripts/build_tcc.sh" "${OMEGA_LIBC_ARCH}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS omega-libc
        VERBATIM)
endif()
