#!/bin/sh
# Cross-builds d2rl-permanent-cow-portal.dll on Linux with MinGW-w64.
# Needs: x86_64-w64-mingw32-g++-win32 / windres, git. The SDK is pinned to the commit this was built against.
set -e
here=$(cd "$(dirname "$0")" && pwd)
sdk=${D2RL_SDK:-$here/.sdk}
if [ ! -d "$sdk" ]; then
  git clone -q https://github.com/D2RLoader/PluginSDK.git "$sdk"
  git -C "$sdk" checkout -q 03a5af9
fi
cxx=x86_64-w64-mingw32-g++-win32
command -v $cxx >/dev/null || { echo "needs $cxx (apt install g++-mingw-w64-x86-64-win32)"; exit 1; }
out=$here/build
mkdir -p "$out"
x86_64-w64-mingw32-windres -I"$sdk/include" -I"$here" "$here/src/plugin.rc" -O coff -o "$out/plugin.res"
$cxx -std=c++20 -Os -Wall -shared -static -s \
  -I"$sdk/include" "$here/src/plugin.cpp" "$out/plugin.res" -lversion \
  -o "$out/d2rl-permanent-cow-portal.dll"
cp "$out/d2rl-permanent-cow-portal.dll" "$here/"
# Package: the DLL alone (its config is built in; D2RLoader writes it on first launch).
rm -f "$here"/permanent-cow-portal*.zip
(cd "$here" && zip -q -X permanent-cow-portal.zip d2rl-permanent-cow-portal.dll)
echo "built $here/d2rl-permanent-cow-portal.dll and $here/permanent-cow-portal.zip"
