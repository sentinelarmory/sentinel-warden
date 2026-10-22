#!/usr/bin/env bash
# Builds the plugin DLL with MinGW-w64 and lays out a ready-to-install folder:
#   dist/iCUE-G403-Plugin/Install-G403Plugin.ps1
#   dist/iCUE-G403-Plugin/Logitech/{g403_plugin.dll,*.png}
# Requires: x86_64-w64-mingw32-gcc (Debian/Ubuntu: apt install gcc-mingw-w64-x86-64)
set -euo pipefail
cd "$(dirname "$0")"
out=dist/iCUE-G403-Plugin
rm -rf dist
mkdir -p "$out/Logitech"
x86_64-w64-mingw32-gcc -O2 -Wall -shared -static-libgcc -s \
    -o "$out/Logitech/g403_plugin.dll" src/g403_plugin.c -lbcrypt -ladvapi32
cp images/*.png "$out/Logitech/"
cp scripts/Install-G403Plugin.ps1 "$out/"
(cd dist && zip -qr iCUE-G403-Plugin.zip iCUE-G403-Plugin)
echo "built dist/iCUE-G403-Plugin.zip"
