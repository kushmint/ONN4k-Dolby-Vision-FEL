#!/usr/bin/env bash
# Shared build settings for the dvfel module. Source this file; do not run it.
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
O="$ROOT/out"
export PATH="$ROOT/clang-r487747c/bin:$PATH"
export BRANCH=android14-5.15 KMI_GENERATION=11 BUILD_NUMBER=14657224
STUB="$ROOT/tools/openssl_engine_stub.o"
M=(make "-j$(nproc)" -C "$ROOT/common" ARCH=arm64 LLVM=1 LLVM_IAS=1 "O=$O"
   "HOSTLDLIBS_extract-cert=-lcrypto $STUB"
   "HOSTLDLIBS_sign-file=-lcrypto $STUB")
