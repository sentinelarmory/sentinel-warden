#!/usr/bin/env bash
# Builds the plugin DLL with MinGW-w64 and lays out a ready-to-install folder:
#   dist/iCUE-Logitech-Plugin/Install-LogitechPlugin.ps1
#   dist/iCUE-Logitech-Plugin/Logitech/{logitech_plugin.dll,plugin.ini,*.png}
# Requires: x86_64-w64-mingw32-gcc (Debian/Ubuntu: apt install gcc-mingw-w64-x86-64)
set -euo pipefail
cd "$(dirname "$0")"
out=dist/iCUE-Logitech-Plugin
rm -rf dist
mkdir -p "$out/Logitech"
x86_64-w64-mingw32-gcc -O2 -Wall -shared -static-libgcc -s \
    -o "$out/Logitech/logitech_plugin.dll" src/logitech_plugin.c -lbcrypt -ladvapi32
cp images/*.png plugin.ini "$out/Logitech/"
cp scripts/Install-LogitechPlugin.ps1 "$out/"
(cd dist && zip -qr iCUE-Logitech-Plugin.zip iCUE-Logitech-Plugin)
echo "built dist/iCUE-Logitech-Plugin.zip"
