#!/bin/sh
# Cross-builds d2rl-compare-indicators.dll on Linux with MinGW-w64.
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
  -o "$out/d2rl-compare-indicators.dll"
cp "$out/d2rl-compare-indicators.dll" "$here/"
# D2RMM package: the DLL alone (its default config is embedded; D2RLoader warns on loose files).
version=$(sed -n 's/.*PluginVersion = "\(.*\)";/\1/p' "$here/src/plugin.cpp")
zip=compare-indicators-$version.zip
rm -f "$here"/compare-indicators*.zip
(cd "$here" && zip -q -X "$zip" d2rl-compare-indicators.dll)
echo "built $here/d2rl-compare-indicators.dll and $here/$zip"
