#!/bin/sh
# OpenJK VR dedicated server (Linux x86_64). See README-SERVER.txt.
#
#   ./start-server.sh                     an internet server (also listed in
#                                         the game's server browser)
#   ./start-server.sh +set dedicated 1    only for your own network (LAN)
#
# Settings: base/server.cfg. Type server commands in this terminal (status,
# map mp/ffa3, kick <name>, addbot <name>, svsay <text>, quit ...). Extra
# arguments are passed on, e.g.  ./start-server.sh +set net_port 29071
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE" || exit 1

missing=""
for n in 0 1 2 3; do
	[ -f "base/assets$n.pk3" ] || missing="$missing assets$n.pk3"
done
if [ -n "$missing" ]; then
	echo "Missing from $HERE/base:$missing"
	echo "Copy assets0.pk3 - assets3.pk3 from your own Jedi Academy"
	echo "(its GameData/base folder) into it."
	exit 1
fi

exec ./openjkded.x86_64 +set fs_basepath "$HERE" +set fs_homepath "$HERE/home" \
	+set dedicated 2 +set net_port 29070 +exec server.cfg "$@"
