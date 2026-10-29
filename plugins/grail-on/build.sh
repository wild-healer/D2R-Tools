#!/bin/sh
# Cross-builds d2rl-grail-on.dll on Linux with MinGW-w64.
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
x86_64-w64-mingw32-windres -I"$sdk/include" -I"$here" -I"$here/src" "$here/src/plugin.rc" -O coff -o "$out/plugin.res"
x86_64-w64-mingw32-g++ -std=c++20 -O2 -Wall -shared -static -s \
  -I"$sdk/include" "$here/src/plugin.cpp" "$out/plugin.res" -lversion -lshell32 -lole32 -luuid -lwindowscodecs \
  -o "$out/d2rl-grail-on.dll"
cp "$out/d2rl-grail-on.dll" "$here/"
# D2RLoader package: only the DLL (icon credits are embedded as a resource). No toml: the settings page writes its own file in Saved Games.
version=$(sed -n 's/.*PluginVersion = "\(.*\)";/\1/p' "$here/src/plugin.cpp")
zip=grail-on-$version.zip
rm -f "$here"/grail-on*.zip
rm -f "$here/ICONS-CREDITS.txt"
(cd "$here" && zip -q -X "$zip" d2rl-grail-on.dll)
echo "built $here/d2rl-grail-on.dll and $here/$zip"
