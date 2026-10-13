#!/bin/sh
# Builds OpenJK VR (multiplayer) for Windows x64 from Linux with llvm-mingw
# (https://github.com/mstorsjo/llvm-mingw), and assembles the OpenJK-VR folder
# that goes into Jedi Academy's GameData folder: the game, and the dedicated
# server (openjkded.x86_64.exe, "Launch OpenJK VR Server.bat").
#
#   LLVM_MINGW=/opt/llvm-mingw vr/build-win64.sh
#
# Output: build-win64/OpenJK-VR/
set -e

LLVM_MINGW=${LLVM_MINGW:-/opt/llvm-mingw}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build-win64"
OUT="$BUILD/OpenJK-VR"

cat > "$BUILD.toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $LLVM_MINGW/bin/x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER $LLVM_MINGW/bin/x86_64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER $LLVM_MINGW/bin/x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH $LLVM_MINGW/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$BUILD.toolchain.cmake" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_EXE_LINKER_FLAGS=-static -DCMAKE_SHARED_LINKER_FLAGS=-static \
	-DBuildMPVR=ON -DBuildMPRend2=OFF -DBuildMPDed=ON \
	-DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF \
	-DBuildJK2SPEngine=OFF -DBuildJK2SPGame=OFF -DBuildJK2SPRdVanilla=OFF
ninja -C "$BUILD"

rm -rf "$OUT"
mkdir -p "$OUT/base"
cp "$BUILD/openjk.x86_64.exe" "$BUILD/rd-vanilla_x86_64.dll" "$BUILD/openjkded.x86_64.exe" "$OUT/"
cp "$BUILD/cgamex86_64.dll" "$BUILD/uix86_64.dll" "$BUILD/jampgamex86_64.dll" "$OUT/base/"
# the voice chat settings for the Sound page of Setup (ui_vrvoice.c reads it)
mkdir -p "$OUT/base/ui"
cp "$ROOT/vr/ui/vr_voice.menu" "$OUT/base/ui/"
cp "$ROOT/lib/SDL2/bin/x64/SDL2.dll" "$ROOT/lib/openxr/bin/x64/libopenxr_loader.dll" "$OUT/"
cp "$ROOT/vr/Launch OpenJK VR.bat" "$ROOT/vr/Launch OpenJK Desktop.bat" "$ROOT/vr/README-VR.txt" "$OUT/"
# the dedicated server: its launcher, settings and notes
cp "$ROOT/vr/Launch OpenJK VR Server.bat" "$ROOT/vr/server/README-SERVER.txt" "$OUT/"
cp "$ROOT/vr/server/server.cfg" "$OUT/base/"
cp "$ROOT/LICENSE.txt" "$OUT/LICENSE-OpenJK-GPLv2.txt"
cp "$ROOT/lib/openxr/LICENSE" "$OUT/LICENSE-OpenXR-loader.txt"
cp "$ROOT/lib/opus/COPYING" "$OUT/LICENSE-Opus.txt"
# Windows line endings for the files people open in Notepad / cmd
for f in "$OUT/Launch OpenJK VR.bat" "$OUT/Launch OpenJK Desktop.bat" "$OUT/Launch OpenJK VR Server.bat" \
	"$OUT/README-VR.txt" "$OUT/README-SERVER.txt" "$OUT/base/server.cfg" "$OUT/LICENSE-Opus.txt"; do
	sed -i 's/\r*$/\r/' "$f"
done
echo "Built $OUT"
