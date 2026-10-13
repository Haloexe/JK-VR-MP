#!/bin/sh
# Builds the OpenJK VR dedicated server for Linux x86_64 - openjkded.x86_64
# and base/jampgamex86_64.so, the server game with the VR saber combat, the VR
# body relay and voice chat - and assembles the folder to copy to a server.
#
#   vr/build-linux-server.sh
#
# Needs gcc/g++, cmake and ninja on any x86_64 Linux. The result runs on
# glibc 2.34 and newer (Ubuntu 22.04, Debian 12, RHEL 9 and later) whatever
# it was built on: libstdc++ is linked in, vr/server/linux-glibc-compat.h and
# .c keep out what newer glibcs added, and the end of this script checks it.
#
# Output: build-linux-server/OpenJK-VR-Server-linux/ and a .tar.gz of it
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build-linux-server"
NAME=OpenJK-VR-Server-linux
OUT="$BUILD/$NAME"
COMPAT_H="$ROOT/vr/server/linux-glibc-compat.h"
COMPAT_O="$BUILD/linux-glibc-compat.o"

mkdir -p "$BUILD"
cc -O2 -fPIC -Wall -c "$ROOT/vr/server/linux-glibc-compat.c" -o "$COMPAT_O"
LINK="-static-libstdc++ -Wl,--exclude-libs,ALL $COMPAT_O"

cmake -S "$ROOT" -B "$BUILD" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_FLAGS="-include $COMPAT_H" -DCMAKE_CXX_FLAGS="-include $COMPAT_H" \
	-DCMAKE_EXE_LINKER_FLAGS="$LINK" -DCMAKE_SHARED_LINKER_FLAGS="$LINK" \
	-DUseInternalJPEG=ON -DUseInternalZlib=ON -DUseInternalPNG=ON \
	-DBuildMPDed=ON -DBuildMPGame=ON \
	-DBuildMPEngine=OFF -DBuildMPRdVanilla=OFF -DBuildMPRend2=OFF -DBuildMPVR=OFF \
	-DBuildMPCGame=OFF -DBuildMPUI=OFF \
	-DBuildSPEngine=OFF -DBuildSPGame=OFF -DBuildSPRdVanilla=OFF \
	-DBuildJK2SPEngine=OFF -DBuildJK2SPGame=OFF -DBuildJK2SPRdVanilla=OFF
ninja -C "$BUILD"

rm -rf "$OUT" "$OUT.tar.gz"
mkdir -p "$OUT/base"
cp "$BUILD/openjkded.x86_64" "$OUT/"
cp "$BUILD/codemp/game/jampgamex86_64.so" "$OUT/base/"
cp "$ROOT/vr/server/server.cfg" "$OUT/base/"
cp "$ROOT/vr/server/start-server.sh" "$ROOT/vr/server/README-SERVER.txt" "$OUT/"
cp "$ROOT/LICENSE.txt" "$OUT/LICENSE-OpenJK-GPLv2.txt"
# its source code (GPL): the commit it was built from
if git -C "$ROOT" rev-parse --verify -q HEAD >/dev/null; then
	mkdir -p "$OUT/source"
	git -C "$ROOT" archive --format=zip --prefix=OpenJK-VR-src/ -o "$OUT/source/OpenJK-VR-src.zip" HEAD
	git -C "$ROOT" diff --quiet HEAD -- || echo "Note: uncommitted changes are not in source/OpenJK-VR-src.zip" >&2
fi
chmod 755 "$OUT/openjkded.x86_64" "$OUT/start-server.sh"
chmod 644 "$OUT/base/jampgamex86_64.so"
# Unix line endings for the files read on the server
sed -i 's/\r$//' "$OUT/base/server.cfg" "$OUT/start-server.sh" "$OUT/README-SERVER.txt"

# it must run on glibc 2.34 without the system's libstdc++
for f in "$OUT/openjkded.x86_64" "$OUT/base/jampgamex86_64.so"; do
	NEWER=$(objdump -T "$f" | grep -oE "GLIBC_2\.(3[5-9]|[4-9][0-9])" | sort -u | tr '\n' ' ')
	if [ -n "$NEWER" ]; then
		echo "$f needs a glibc newer than 2.34: $NEWER" >&2
		exit 1
	fi
	if objdump -T "$f" | grep -q "GLIBCXX_\|CXXABI_"; then
		echo "$f needs the system's libstdc++" >&2
		exit 1
	fi
done

tar -C "$BUILD" --owner=0 --group=0 -czf "$OUT.tar.gz" "$NAME"
echo "Built $OUT"
