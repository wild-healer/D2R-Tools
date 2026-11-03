#!/bin/sh
# Cross-builds d2rl-perfect-frame.dll on Linux with MinGW-w64.
# Needs: x86_64-w64-mingw32-g++ / windres, git. The SDK is pinned to the commit this was built against.
set -e
here=$(cd "$(dirname "$0")" && pwd)
sdk=${D2RL_SDK:-$here/.sdk}
if [ ! -d "$sdk" ]; then
  git clone -q https://github.com/D2RLoader/PluginSDK.git "$sdk"
  git -C "$sdk" checkout -q 03a5af9
fi
# The win32 thread model (no winpthreads) and -Os: the posix/-O2 builds were
# flagged by one antivirus machine-learning engine.
cxx=x86_64-w64-mingw32-g++-win32
command -v $cxx >/dev/null || { echo "needs $cxx (apt install g++-mingw-w64-x86-64-win32)"; exit 1; }
out=$here/build
mkdir -p "$out"
x86_64-w64-mingw32-windres -I"$sdk/include" -I"$here" "$here/src/plugin.rc" -O coff -o "$out/plugin.res"
$cxx -std=c++20 -Os -Wall -shared -static -s \
  -I"$sdk/include" "$here/src/plugin.cpp" "$out/plugin.res" -lversion \
  -o "$out/d2rl-perfect-frame.dll"
cp "$out/d2rl-perfect-frame.dll" "$here/"
# Package: the DLL alone (its config is built in; D2RLoader writes it on first launch).
rm -f "$here"/perfect-frame*.zip
(cd "$here" && zip -q -X perfect-frame.zip d2rl-perfect-frame.dll)
echo "built $here/d2rl-perfect-frame.dll and $here/perfect-frame.zip"
