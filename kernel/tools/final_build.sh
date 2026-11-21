#!/usr/bin/env bash
# Check header layouts, build dvfel.ko with exact symbol versions, and verify it.
set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
P=$(cd "$ROOT/.." && pwd)
B="$ROOT/build/dvfel"
L="$ROOT/build/layout"

printf 'obj-m := layout_check.o\nccflags-y += -I%s/common_drivers/include\n' "$ROOT" > "$L/Kbuild"
"${M[@]}" M="$L" KBUILD_MODPOST_WARN=1 modules > "$L/build.log" 2>&1
echo "layout check exit: $?"
grep -E 'error:' "$L/build.log" | sed "s#$L/##" | cut -c1-170 | head -20

echo "=== stock provider ops offsets"
for k in firmware/aml_media.ko firmware/us/aml_media.ko; do
  for f in vf_peek vf_get vf_put; do
    printf '%s %s: ' "$k" "$f"
    llvm-objdump -d --no-show-raw-insn "--disassemble-symbols=$f" "$P/$k" |
      grep -E 'ldr[[:space:]]+x[0-9]+, \[x[0-9]+(, #0x[0-9a-f]+)?\]' |
      sed -E 's/.*ldr[[:space:]]+//' | tr '\n' ';' | cut -c1-120
    echo
  done
done

grep -q 'module_layout' "$B/dvfel-deps.symvers" ||
  printf '0x4bcc9336\tmodule_layout\tvmlinux\tEXPORT_SYMBOL\t\n' >> "$B/dvfel-deps.symvers"
cp "$P"/src/fel/onn/* "$B/"
touch "$B/dvfel_s7d.c"
"${M[@]}" M="$B" "KBUILD_EXTRA_SYMBOLS=$B/dvfel-deps.symvers" modules > "$B/build.log" 2>&1
echo "strict build exit: $?"
grep -E 'error:|undefined!' "$B/build.log" | cut -c1-150 | head
python3 -I "$ROOT/tools/verify_versions.py" "$B/dvfel.ko" "$B/dvfel-deps.symvers"
echo "verify exit: $?"
mkdir -p "$P/dist"
# Debug info carries build paths; dist/ gets a stripped copy (kernel/build keeps the full one).
llvm-strip --strip-debug -o "$P/dist/dvfel.ko" "$B/dvfel.ko"
strings -a "$P/dist/dvfel.ko" | grep -E '^(vermagic|depends)='
llvm-nm "$P/dist/dvfel.ko" | grep -E ' __cfi_check$'
ls -la "$P/dist/dvfel.ko"
sha256sum "$P/dist/dvfel.ko"
