#!/bin/sh
# Cross-builds the D2R GUI Demo on Linux with MinGW-w64. The D2R GUI library
# (lib/d2r-gui, header-only) is compiled into the DLL.
set -e
here=$(cd "$(dirname "$0")" && pwd)
gui=$here/../../lib/d2r-gui/include
sdk=${D2RL_SDK:-$here/.sdk}
if [ ! -d "$sdk" ]; then
  git clone -q https://github.com/D2RLoader/PluginSDK.git "$sdk"
  git -C "$sdk" checkout -q 03a5af9
fi
out=$here/build
mkdir -p "$out"
x86_64-w64-mingw32-windres -I"$sdk/include" "$here/src/plugin.rc" -O coff -o "$out/plugin.res"
x86_64-w64-mingw32-g++ -std=c++20 -O2 -Wall -shared -static -s -I"$sdk/include" -I"$gui" "$here/src/plugin.cpp" "$out/plugin.res" -o "$out/d2rl-d2r-gui-demo.dll"
rm -f "$here"/d2r-gui-demo*.zip
(cd "$out" && zip -q -X "$here/d2r-gui-demo.zip" d2rl-d2r-gui-demo.dll)
echo "built $here/d2r-gui-demo.zip"
