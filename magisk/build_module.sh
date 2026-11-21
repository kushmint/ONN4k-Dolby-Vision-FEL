#!/usr/bin/env bash
# Package the Magisk module zip: kernel module, scripts and the settings app.
set -euo pipefail
R=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
M=$R/magisk/onn_dvfel
cp "$R/dist/dvfel.ko" "$M/dvfel.ko"
sed 's#^SAVED=/data/local/tmp/lldv.saved#SAVED=/data/adb/onn_dvfel/lldv.saved#' "$R/tests/lldv_setup_device.sh" > "$M/lldv_setup.sh"
grep -q '^SAVED=/data/adb/onn_dvfel/lldv.saved' "$M/lldv_setup.sh"
cp "$R/player/build/DvfelSettings.apk" "$M/system/app/DvfelSettings/DvfelSettings.apk"
for f in service.sh dvfelctl lldv_setup.sh customize.sh; do sh -n "$M/$f"; done
OUT=$R/dist/onn-dvfel-magisk-$(grep ^version= "$M/module.prop" | cut -d= -f2).zip
rm -f "$OUT"
python3 -I - "$M" "$OUT" <<'PY'
import os, sys, zipfile
src, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for d, _, files in os.walk(src):
        for f in sorted(files):
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, src))
PY
python3 -I -c "import sys,zipfile; [print(i.filename, i.file_size) for i in zipfile.ZipFile(sys.argv[1]).infolist()]" "$OUT"
sha256sum "$OUT"
