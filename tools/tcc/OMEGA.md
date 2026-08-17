# TinyCC on Omega

This directory contains the upstream TinyCC source and its upstream LGPL-2.1
license. Omega builds one static `tcc` executable per reference ISA using the
matching `libc/omega-sdk/<arch>` sysroot. The compiler is a separate shipped
userspace binary; it is not linked into the kernel.

The supported build entry point is:

```sh
scripts/build_tcc.sh x86_64
scripts/build_tcc.sh aarch64
scripts/build_tcc.sh riscv64
```

The resulting compiler is placed in `libc/omega-sdk/<arch>/bin/tcc`; build
intermediates are placed in `build/tcc-<arch>/`. AArch64 and RISC-V also
receive `lib/libtcc1.a`, built with the target ABI, in the SDK. The compiler is
configured to search the Omega sysroot. Static output is the supported mode;
dynamic linking and `tcc -run` remain dependent on the corresponding kernel
runtime support.
