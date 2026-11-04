#!/bin/sh
# Cross-builds d2rl-shrine-icons.dll on Linux with MinGW-w64.
# Needs: x86_64-w64-mingw32-g++ / windres, git. The SDK is pinned to the commit this was built against.
set -e
here=$(cd "$(dirname "$0")" && pwd)
sdk=${D2RL_SDK:-$here/.sdk}
if [ ! -d "$sdk" ]; then
  git clone -q https://github.com/D2RLoader/PluginSDK.git "$sdk"
  git -C "$sdk" checkout -q 03a5af9
fi
out=$here/build
mkdir -p "$out"
x86_64-w64-mingw32-windres -I"$sdk/include" -I"$here" "$here/src/plugin.rc" -O coff -o "$out/plugin.res"
x86_64-w64-mingw32-g++-win32 -std=c++20 -Os -Wall -shared -static -s \
  -I"$sdk/include" "$here/src/plugin.cpp" "$out/plugin.res" -lversion \
  -o "$out/d2rl-shrine-icons.dll"
cp "$out/d2rl-shrine-icons.dll" "$here/"
# Package: the DLL alone (its config and minimap art are built in; D2RLoader
# writes the config on first launch).
rm -f "$here"/shrine-icons*.zip
(cd "$here" && zip -q -X shrine-icons.zip d2rl-shrine-icons.dll)
echo "built $here/d2rl-shrine-icons.dll and $here/shrine-icons.zip"
