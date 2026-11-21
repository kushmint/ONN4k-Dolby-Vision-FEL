#!/usr/bin/env bash
# Build libdovi, libdvfelplay and the dvfp_test APK for the ONN (armeabi-v7a, API 26).
set -euo pipefail
P=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
NDK=$(ls -d "$HOME"/android-ndk/android-ndk-r28c)
SDK=$P/sdk
BT=$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)
PLATFORM=$(ls -d "$SDK"/platforms/* | sort -V | tail -1)
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
# libdvfelplay lives in the players project (branch onn-4k-pro of signde/vlc-fel-android)
SRC=${DVFELPLAY_SRC:-$P/../../ONN4k-FEL-Players/vlc-fel-android/libdvfelplay}
OUT=${OUT:-$P/build}
# keep build-machine paths (home dir, cargo registry) out of the binaries
REMAP_RUST="--remap-path-prefix=$HOME/.cargo/registry/src=/cargo --remap-path-prefix=$HOME=/build"
REMAP_C="-ffile-prefix-map=$HOME=/build"
DOVI=$OUT/libdovi
mkdir -p "$OUT"

echo "=== tools"
"$TC/clang" --version | head -1
ls "$BT/aapt2" "$BT/zipalign" "$BT/apksigner" "$PLATFORM/android.jar" >/dev/null
rustup target list --installed | grep -x armv7-linux-androideabi
cargo cinstall --version | head -1

echo "=== libdovi"
if [ ! -f "$DOVI/lib/libdovi.a" ]; then
  [ -d "$OUT/dovi_tool" ] || git clone -q --depth=1 --branch libdovi-3.3.2 https://github.com/quietvoid/dovi_tool.git "$OUT/dovi_tool"
  export CARGO_TARGET_ARMV7_LINUX_ANDROIDEABI_LINKER="$TC/armv7a-linux-androideabi26-clang"
  export CC_armv7_linux_androideabi="$TC/armv7a-linux-androideabi26-clang"
  export AR_armv7_linux_androideabi="$TC/llvm-ar"
  export CARGO_TARGET_ARMV7_LINUX_ANDROIDEABI_RUSTFLAGS="$REMAP_RUST"
  export CFLAGS_armv7_linux_androideabi="$REMAP_C"
  (cd "$OUT/dovi_tool/dolby_vision" &&
   cargo cinstall --release --target armv7-linux-androideabi --library-type staticlib \
     --prefix "$DOVI" --libdir "$DOVI/lib" --includedir "$DOVI/include")
fi
ls -la "$DOVI/lib/libdovi.a" "$DOVI/include/libdovi/rpu_parser.h"

echo "=== libdvfelplay and test library"
cmake -S "$SRC" -B "$OUT/cmake" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static \
  -DCMAKE_C_FLAGS="$REMAP_C" -DCMAKE_CXX_FLAGS="$REMAP_C" \
  -DLIBDOVI_DIR="$DOVI" >/dev/null
cmake --build "$OUT/cmake" --target dvfelplay dvfptest

echo "=== APK"
KS=$P/debug.keystore
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -keypass android -alias debug \
  -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=dvfel test"
A=$OUT/apk
mkdir -p "$A/lib/armeabi-v7a"
cp "$OUT/cmake/libdvfelplay.so" "$OUT/cmake/libdvfptest.so" "$A/lib/armeabi-v7a/"
"$BT/aapt2" link --manifest "$SRC/test/AndroidManifest.xml" -I "$PLATFORM/android.jar" -o "$A/base.apk"
python3 -I - "$A" <<'PY'
import pathlib, sys, zipfile
root = pathlib.Path(sys.argv[1])
with zipfile.ZipFile(root / "base.apk", "a", zipfile.ZIP_DEFLATED) as apk:
    present = set(apk.namelist())
    for lib in sorted((root / "lib").rglob("*.so")):
        name = lib.relative_to(root).as_posix()
        if name not in present:
            apk.write(lib, name)
PY
"$BT/zipalign" -f -p 4 "$A/base.apk" "$A/aligned.apk"
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
  --out "$OUT/dvfptest.apk" "$A/aligned.apk"
"$BT/apksigner" verify "$OUT/dvfptest.apk" && echo "APK signature ok"
ls -la "$OUT/cmake/libdvfelplay.so" "$OUT/dvfptest.apk"
