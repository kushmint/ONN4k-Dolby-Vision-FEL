#!/usr/bin/env bash
# Build the Dolby Vision FEL settings app (plain Java, no Gradle).
set -euo pipefail
P=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
A=$P/settings-app
SDK=$P/sdk
BT=$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)
JAR=$(ls -d "$SDK"/platforms/* | sort -V | tail -1)/android.jar
O=$P/build/settings
rm -rf "$O"; mkdir -p "$O/classes" "$O/res" "$O/dex"

# Banner and icon drawn with ffmpeg so no binary art lives in the repo.
FONT=$(fc-match -f '%{file}' 'DejaVu Sans:bold' 2>/dev/null || true)
mkdir -p "$A/res/drawable"
ffmpeg -v error -y -f lavfi -i color=c=0x101828:s=320x180 -frames:v 1 \
  -vf "drawtext=fontfile=$FONT:text='Dolby Vision':fontcolor=white:fontsize=34:x=(w-tw)/2:y=52,drawtext=fontfile=$FONT:text='FEL settings':fontcolor=0x78B4FF:fontsize=28:x=(w-tw)/2:y=100" \
  "$A/res/drawable/banner.png"
ffmpeg -v error -y -f lavfi -i color=c=0x101828:s=192x192 -frames:v 1 \
  -vf "drawtext=fontfile=$FONT:text='FEL':fontcolor=0x78B4FF:fontsize=72:x=(w-tw)/2:y=(h-th)/2" \
  "$A/res/drawable/icon.png"

"$BT/aapt2" compile --dir "$A/res" -o "$O/res.zip"
"$BT/aapt2" link -I "$JAR" --manifest "$A/AndroidManifest.xml" -o "$O/base.apk" "$O/res.zip"
javac --release 11 -nowarn -XDstringConcat=inline -cp "$JAR" -d "$O/classes" $(find "$A/src" -name '*.java')
java -cp "$P/sdk/r8/r8.jar" com.android.tools.r8.D8 --release --min-api 26 --lib "$JAR" --output "$O/dex" $(find "$O/classes" -name '*.class')
python3 -I -c "import sys,zipfile; zipfile.ZipFile(sys.argv[1],'a',zipfile.ZIP_DEFLATED).write(sys.argv[2],'classes.dex')" "$O/base.apk" "$O/dex/classes.dex"
"$BT/zipalign" -f -p 4 "$O/base.apk" "$O/aligned.apk"
KS=$P/debug.keystore
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android \
  --out "$P/build/DvfelSettings.apk" "$O/aligned.apk"
"$BT/apksigner" verify "$P/build/DvfelSettings.apk" && echo "settings APK ok: $P/build/DvfelSettings.apk"
