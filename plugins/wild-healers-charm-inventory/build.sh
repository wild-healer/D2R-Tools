#!/bin/sh
# Cross-builds wild-healers-charm-inventory.dll on Linux with MinGW-w64.
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
x86_64-w64-mingw32-g++ -std=c++20 -O2 -Wall -shared -static -s -I"$sdk/include" \
  "$here/src/plugin.cpp" "$out/plugin.res" -o "$out/wild-healers-charm-inventory.dll"
# D2RMM package: the DLL alone (its default config is embedded).
version=$(sed -n 's/.*PluginVersion = "\(.*\)";/\1/p' "$here/src/plugin.cpp")
rm -f "$here"/*.zip
(cd "$out" && zip -q -X "$here/wild-healers-charm-inventory-$version.zip" wild-healers-charm-inventory.dll)
echo "built $here/wild-healers-charm-inventory-$version.zip"
