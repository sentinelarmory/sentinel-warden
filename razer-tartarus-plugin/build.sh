#!/usr/bin/env bash
# Builds the plugin DLL with MinGW-w64 and lays out a ready-to-install folder:
#   dist/iCUE-Tartarus-Plugin/Install-TartarusPlugin.ps1
#   dist/iCUE-Tartarus-Plugin/Razer/{tartarus_plugin.dll,plugin.ini,*.png}
# Requires: x86_64-w64-mingw32-gcc (Debian/Ubuntu: apt install gcc-mingw-w64-x86-64 zip)
set -euo pipefail
cd "$(dirname "$0")"
out=dist/iCUE-Tartarus-Plugin
rm -rf dist
mkdir -p "$out/Razer"
x86_64-w64-mingw32-gcc -O2 -Wall -shared -static-libgcc -s \
    -o "$out/Razer/tartarus_plugin.dll" src/tartarus_plugin.c \
    -lbcrypt -ladvapi32 -lhid -lsetupapi -lwinmm
cp images/*.png plugin.ini "$out/Razer/"
cp scripts/Install-TartarusPlugin.ps1 "$out/"
(cd dist && zip -qr iCUE-Tartarus-Plugin.zip iCUE-Tartarus-Plugin)
echo "built dist/iCUE-Tartarus-Plugin.zip"
