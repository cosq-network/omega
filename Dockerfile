# Multi-stage Dockerfile for Omega Kernel Development Environment
# Minimal image size based on Alpine Linux with Clang, CMake, LLVM, QEMU, and Tcl/Tk support

FROM alpine:3.19 AS dev-environment

LABEL maintainer="Omega Kernel Team <benoybose@cosq.net>"
LABEL description="Minimal bare-metal C++20 cross-compilation environment for Omega OS"

# Install minimal build tools, Clang/LLVM cross-compilation toolchain, QEMU emulators, mtools, and Tcl/Tk
RUN apk add --no-no-cache \
    bash \
    build-base \
    cmake \
    ninja \
    clang17 \
    llvm17 \
    lld \
    qemu-system-x86_64 \
    qemu-system-aarch64 \
    qemu-system-riscv64 \
    qemu-img \
    mtools \
    dosfstools \
    xorriso \
    tcl \
    tk \
    git \
    make \
    rm -rf /var/cache/apk/*

# Create non-root workspace directory
WORKDIR /workspace

# Set default shell to bash
SHELL ["/bin/bash", "-c"]

CMD ["/bin/bash"]
