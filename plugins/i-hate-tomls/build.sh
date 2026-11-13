#!/bin/sh
# Cross-builds I Hate TOMLs and its demo client on Linux with MinGW-w64.
set -e
here=$(cd "$(dirname "$0")" && pwd)
sdk=${D2RL_SDK:-$here/.sdk}
if [ ! -d "$sdk" ]; then
  git clone -q https://github.com/D2RLoader/PluginSDK.git "$sdk"
  git -C "$sdk" checkout -q 03a5af9
fi
out=$here/build
mkdir -p "$out"
gui=$here/../../lib/d2r-gui/include
cc() { x86_64-w64-mingw32-g++ -std=c++20 -O2 -Wall -shared -static -s -I"$sdk/include" -I"$gui" "$@"; }
x86_64-w64-mingw32-windres -I"$sdk/include" "$here/src/plugin.rc" -O coff -o "$out/plugin.res"
cc "$here/src/plugin.cpp" "$out/plugin.res" -o "$out/aaa-i-hate-tomls.dll" -lwinhttp -lshell32
x86_64-w64-mingw32-windres -I"$sdk/include" -I"$here/demo" "$here/demo/demo.rc" -O coff -o "$out/demo.res"
cc "$here/demo/demo.cpp" "$out/demo.res" -o "$out/d2rl-i-hate-tomls-demo.dll"
x86_64-w64-mingw32-windres -I"$sdk/include" "$here/demo/window_demo.rc" -O coff -o "$out/window_demo.res"
cc "$here/demo/window_demo.cpp" "$out/window_demo.res" -o "$out/d2rl-i-hate-tomls-window-demo.dll"
rm -f "$here"/i-hate-tomls*.zip "$here"/plugin-menu*.zip
(cd "$out" && zip -q -X "$here/i-hate-tomls.zip" aaa-i-hate-tomls.dll)
(cd "$out" && cp "$here/demo/i-hate-tomls-demo.toml" . && zip -q -X "$here/i-hate-tomls-demo.zip" d2rl-i-hate-tomls-demo.dll i-hate-tomls-demo.toml d2rl-i-hate-tomls-window-demo.dll)
echo "built $here/i-hate-tomls.zip and i-hate-tomls-demo.zip"
