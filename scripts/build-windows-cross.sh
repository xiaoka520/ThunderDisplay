#!/bin/bash
set -euo pipefail
task_root="$(cd "$(dirname "$0")/.." && pwd)"
task_toolchain="${1:?Usage: build-windows-cross.sh /absolute/path/to/llvm-mingw}"
task_compiler="$task_toolchain/bin/x86_64-w64-mingw32-clang++"
if [ ! -x "$task_compiler" ]; then
    printf 'LLVM MinGW x64 compiler not found: %s\n' "$task_compiler" >&2
    exit 1
fi
mkdir -p "$task_root/build/package/windows-x64/licenses"
"$task_toolchain/bin/x86_64-w64-mingw32-windres" -I "$task_root/windows-client" \
    "$task_root/windows-client/client.rc" "$task_root/build/package/windows-x64/client-resource.o"
"$task_compiler" -mwindows -municode -std=c++17 -O2 -Wall -Wextra -Werror -static \
    -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 \
    -I "$task_root/shared" -I "$task_root/windows-client/src" \
    "$task_root/windows-client/src/displays.cpp" "$task_root/windows-client/src/setup.cpp" "$task_root/build/package/windows-x64/client-resource.o" \
    "$task_root/windows-client/src/main.cpp" "$task_root/windows-client/src/network.cpp" \
    "$task_root/windows-client/src/decoder.cpp" "$task_root/windows-client/src/video_worker.cpp" "$task_root/windows-client/src/renderer.cpp" \
    -o "$task_root/build/package/windows-x64/ThunderDisplayClient.exe" \
    -lws2_32 -liphlpapi -lmfplat -lmf -lmfuuid -lwmcodecdspuuid -ld3d11 -ld3dcompiler -ldxgi -ldxguid \
    -lole32 -loleaut32 -luser32 -lshell32 -lgdi32 -ladvapi32 -lcrypt32 -lcomctl32 -ldwmapi -luxtheme -lwindowscodecs
rm "$task_root/build/package/windows-x64/client-resource.o"
cp "$task_toolchain/LICENSE.TXT" "$task_root/build/package/windows-x64/licenses/LLVM.txt"
cp "$task_root/windows-client/START-HERE.txt" "$task_root/build/package/windows-x64/START-HERE.txt"
cp "$task_root/windows-client/assets/README.md" "$task_root/build/package/windows-x64/licenses/Mac-system-cursor.txt"
cp "$task_toolchain/x86_64-w64-mingw32/share/mingw32/"COPYING* "$task_root/build/package/windows-x64/licenses/"
printf 'Built %s\n' "$task_root/build/package/windows-x64/ThunderDisplayClient.exe"
