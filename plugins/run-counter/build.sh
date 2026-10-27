#!/bin/sh
# Cross-builds d2rl-run-counter.dll on Linux with MinGW-w64.
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
  -I"$sdk/include" "$here/src/plugin.cpp" "$out/plugin.res" \
  -lshell32 -lole32 -luuid -o "$out/d2rl-run-counter.dll"
cp "$out/d2rl-run-counter.dll" "$here/"
# D2RMM package: the DLL and its config at the zip root.
version=$(sed -n 's/.*PluginVersion = "\(.*\)";/\1/p' "$here/src/plugin.cpp")
zip=run-counter-$version.zip
rm -f "$here"/run-counter*.zip
(cd "$here" && zip -q -X "$zip" d2rl-run-counter.dll run-counter.toml)
echo "built $here/d2rl-run-counter.dll and $here/$zip"
