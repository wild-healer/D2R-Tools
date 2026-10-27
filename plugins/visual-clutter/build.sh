#!/bin/sh
# Cross-builds d2rl-visual-clutter.dll on Linux with MinGW-w64.
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
x86_64-w64-mingw32-g++ -std=c++20 -O2 -Wall -shared -static -s \
  -I"$sdk/include" "$here/src/plugin.cpp" "$out/plugin.res" -lversion \
  -o "$out/d2rl-visual-clutter.dll"
cp "$out/d2rl-visual-clutter.dll" "$here/"
# D2RMM package: the DLL and its config at the zip root.
version=$(sed -n 's/.*PluginVersion = "\(.*\)";/\1/p' "$here/src/plugin.cpp")
zip=visual-clutter-$version.zip
rm -f "$here"/visual-clutter*.zip
(cd "$here" && zip -q -X "$zip" d2rl-visual-clutter.dll visual-clutter.toml)
echo "built $here/d2rl-visual-clutter.dll and $here/$zip"
