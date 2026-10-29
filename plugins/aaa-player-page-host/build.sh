#!/bin/sh
# Cross-builds Player Page Host and its two test clients on Linux with MinGW-w64.
set -e
here=$(cd "$(dirname "$0")" && pwd)
sdk=${D2RL_SDK:-$here/.sdk}
if [ ! -d "$sdk" ]; then
  git clone -q https://github.com/D2RLoader/PluginSDK.git "$sdk"
  git -C "$sdk" checkout -q 03a5af9
fi
out=$here/build
mkdir -p "$out/host" "$out/test"
cc="x86_64-w64-mingw32-g++ -std=c++20 -O2 -Wall -shared -static -s -I$sdk/include"
x86_64-w64-mingw32-windres -I"$sdk/include" -I"$here" "$here/src/host.rc" -O coff -o "$out/host.res"
$cc "$here/src/host.cpp" "$out/host.res" -o "$out/host/aaa-player-page-host.dll"
x86_64-w64-mingw32-windres -I"$sdk/include" "$here/src/test_client.rc" -O coff -o "$out/test.res"
# File names: "aa-" loads before the host, "d2rl-" after it.
$cc -DPH_EARLY "$here/src/test_client.cpp" "$out/test.res" -o "$out/test/aa-page-host-test-early.dll"
$cc -DPH_LATE "$here/src/test_client.cpp" "$out/test.res" -o "$out/test/d2rl-page-host-test-late.dll"
# D2RMM package: the DLL alone (its default config is embedded).
rm -f "$here"/*.zip
(cd "$out/host" && zip -q -X "$here/aaa-player-page-host-1.0.0.zip" aaa-player-page-host.dll)
(cd "$out/test" && zip -q -X "$here/page-host-test-1.0.0.zip" *.dll)
cp "$here/src/page_host_api.h" "$out/"
echo "built $here/aaa-player-page-host-1.0.0.zip and $here/page-host-test-1.0.0.zip"
